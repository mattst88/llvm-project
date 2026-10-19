//===-- IA64IfConversion.cpp - Predicate short branches away --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Every instruction has a qualifying predicate and is a no-op when it is
// false, so a branch around a few instructions can be replaced by a compare
// that sets a predicate, with those instructions predicated on it. A compare
// writes two complementary predicates, so both sides of a branch can be
// handled this way.
//
// This handles a block ending in a conditional branch with either a
// triangle (a single-predecessor successor that falls through to the other
// successor) or a diamond (two such successors joining at the same block).
// The successors must be short and contain no predicated instructions. The
// compare replaces the branch, and the successors' instructions follow it.
//
// The pass runs while a conditional branch is still a single instruction
// that includes its compare, and before block placement.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-if-conversion"
#define PASS_NAME "IA64 if-conversion"

STATISTIC(NumTriangles, "Number of branches around one block removed");
STATISTIC(NumDiamonds, "Number of branches between two blocks removed");
STATISTIC(NumReturns, "Number of branches around a return removed");

static cl::opt<unsigned> Limit(
    "ia64-if-conversion-limit", cl::Hidden, cl::init(4),
    cl::desc("The most instructions of one block that a predicate is given "
             "to in place of a branch"));

namespace {

class IA64IfConversion : public MachineFunctionPass {
  const IA64InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const MachineBranchProbabilityInfo *MBPI = nullptr;

public:
  static char ID;

  IA64IfConversion() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  MachineBasicBlock *getOnlySuccessor(MachineBasicBlock &MBB) const;
  bool canQualify(const MachineBasicBlock &MBB) const;
  void qualify(MachineBasicBlock &From, MachineBasicBlock &To, Register Pred,
               const LivePhysRegs &Otherwise, const LivePhysRegs *JoinLive,
               bool KeepReturn) const;
  bool convert(MachineBasicBlock &MBB) const;
  bool mergeWithSuccessor(MachineBasicBlock &MBB) const;
};

} // end anonymous namespace

char IA64IfConversion::ID = 0;

INITIALIZE_PASS_BEGIN(IA64IfConversion, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(IA64IfConversion, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MBB can only be entered from the end of another block.
static bool isPlain(const MachineBasicBlock &MBB) {
  return !MBB.isEntryBlock() && !MBB.isEHPad() && !MBB.hasAddressTaken() &&
         !MBB.isInlineAsmBrIndirectTarget();
}

// MBB's single successor, if MBB ends with a fallthrough or an
// unconditional branch.
MachineBasicBlock *
IA64IfConversion::getOnlySuccessor(MachineBasicBlock &MBB) const {
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 3> Cond;
  if (MBB.succ_size() != 1 || TII->analyzeBranch(MBB, TBB, FBB, Cond, false) ||
      !Cond.empty())
    return nullptr;
  return *MBB.succ_begin();
}

// The unpredicated return MBB ends with, if that is its only exit.
static const MachineInstr *getReturn(const MachineBasicBlock &MBB) {
  MachineBasicBlock::const_iterator Term = MBB.getFirstTerminator();
  if (!MBB.succ_empty() || Term == MBB.end() ||
      Term->getOpcode() != IA64::BR_RET || std::next(Term) != MBB.end() ||
      Term->getOperand(Term->findFirstPredOperandIdx()).getReg() != IA64::P0)
    return nullptr;
  return &*Term;
}

// Whether every instruction in MBB can be predicated: it has a qualifying
// predicate operand that is still p0, reads and writes no other predicate,
// and is not frame setup or destroy code, since the unwind info cannot
// express a conditionally executed prologue or epilogue. Calls expand to
// more than one instruction.
bool IA64IfConversion::canQualify(const MachineBasicBlock &MBB) const {
  unsigned Count = 0;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isTerminator())
      return MI.getOpcode() == IA64::BRUNCOND || &MI == getReturn(MBB);
    if (MI.isMetaInstruction() || MI.isCall() || MI.isInlineAsm() ||
        MI.getOpcode() == IA64::ALLOC || MI.getFlag(MachineInstr::FrameSetup) ||
        MI.getFlag(MachineInstr::FrameDestroy) || ++Count > Limit)
      return false;
    int Idx = MI.findFirstPredOperandIdx();
    if (Idx < 0)
      return false;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isRegMask())
        return false;
      if (MO.isReg() && IA64::PRRegClass.contains(MO.getReg()) &&
          MO.getReg() != IA64::P0)
        return false;
    }
  }
  return true;
}

// Whether MI can run whether or not Pred holds: it cannot fault or change
// anything but allocatable general registers, none of which is live on the
// path that bypasses it (Otherwise) or into the join block (JoinLive). It
// then need not wait for the compare.
static bool canRunAnyway(const MachineInstr &MI, const LivePhysRegs &Otherwise,
                         const LivePhysRegs &JoinLive) {
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  if (MI.mayLoadOrStore() || MI.hasUnmodeledSideEffects() || MI.isBranch() ||
      MI.isReturn() || MI.mayRaiseFPException())
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!IA64::GRRegClass.contains(Reg) || MRI.isReserved(Reg) ||
        Otherwise.contains(Reg) || JoinLive.contains(Reg))
      return false;
  }
  return true;
}

// Move From's instructions to the end of To, predicated on Pred, along with
// From's return if KeepReturn. With JoinLive, those that canRunAnyway are
// left unpredicated.
// A register they write keeps its old value when Pred is false, so the
// write is also a use of the register if it is live then: Otherwise holds
// the registers live on the path that bypasses From.
void IA64IfConversion::qualify(MachineBasicBlock &From, MachineBasicBlock &To,
                               Register Pred, const LivePhysRegs &Otherwise,
                               const LivePhysRegs *JoinLive,
                               bool KeepReturn) const {
  MachineFunction &MF = *To.getParent();
  for (MachineInstr &MI : make_early_inc_range(From)) {
    if (MI.isTerminator() && !(MI.isReturn() && KeepReturn)) {
      MI.eraseFromParent();
      continue;
    }
    MI.removeFromParent();
    To.insert(To.end(), &MI);
    if (MI.isDebugInstr())
      continue;
    for (MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isUse())
        MO.setIsKill(false);
    if (JoinLive && canRunAnyway(MI, Otherwise, *JoinLive))
      continue;
    MI.getOperand(MI.findFirstPredOperandIdx()).setReg(Pred);
    SmallVector<Register, 2> Kept;
    for (MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg().isPhysical())
        continue;
      // The other side may still read the register, so it is no longer killed.
      if (MO.isUse())
        MO.setIsKill(false);
      else if (Otherwise.contains(MO.getReg())) {
        // A dead def now preserves the old value, which is read on the
        // other path.
        MO.setIsDead(false);
        if (!MI.readsRegister(MO.getReg(), TRI))
          Kept.push_back(MO.getReg());
      }
    }
    for (Register Reg : Kept)
      MI.addOperand(MF, MachineOperand::CreateReg(Reg, /*isDef=*/false,
                                                  /*isImp=*/true));
  }
}

bool IA64IfConversion::convert(MachineBasicBlock &MBB) const {
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 3> Cond;
  if (MBB.succ_size() != 2 || TII->analyzeBranch(MBB, TBB, FBB, Cond, false) ||
      Cond.size() != 3)
    return false;
  if (!FBB)
    FBB = *MBB.succ_begin() == TBB ? *MBB.succ_rbegin() : *MBB.succ_begin();
  if (TBB == FBB || TBB == &MBB || FBB == &MBB)
    return false;

  // Find the sides to predicate and their successors.
  auto CanBeSide = [&](MachineBasicBlock *Succ) {
    return Succ->pred_size() == 1 && isPlain(*Succ) && canQualify(*Succ);
  };
  bool CanT = CanBeSide(TBB), CanF = CanBeSide(FBB);
  MachineBasicBlock *TNext = CanT ? getOnlySuccessor(*TBB) : nullptr;
  MachineBasicBlock *FNext = CanF ? getOnlySuccessor(*FBB) : nullptr;
  const MachineInstr *TReturn = CanT ? getReturn(*TBB) : nullptr;
  const MachineInstr *FReturn = CanF ? getReturn(*FBB) : nullptr;
  MachineBasicBlock *Join = nullptr;
  MachineInstr *Return = nullptr;
  bool QualifyT, QualifyF, Returns = false, Early = false;
  if (TNext && TNext == FNext && TNext != TBB && TNext != FBB) {
    Join = TNext;
    QualifyT = QualifyF = true;
    ++NumDiamonds;
  } else if (TReturn && FReturn && TReturn->isIdenticalTo(*FReturn)) {
    // Both sides end in identical returns; one serves both.
    Returns = QualifyT = QualifyF = true;
    ++NumDiamonds;
  } else {
    // A rarely taken side is cheaper to branch around than to execute as
    // no-ops.
    auto Worthwhile = [&](MachineBasicBlock *Succ) {
      return MBPI->getEdgeProbability(&MBB, Succ) >= BranchProbability(1, 8);
    };
    QualifyT = (TNext == FBB || TReturn) && Worthwhile(TBB);
    QualifyF = !QualifyT && (FNext == TBB || FReturn) && Worthwhile(FBB);
    if (!QualifyT && !QualifyF)
      return false;
    Join = QualifyT ? FBB : TBB;
    // A side that returns keeps its return, predicated.
    Early = QualifyT ? TReturn : FReturn;
    ++(Early ? NumReturns : NumTriangles);
  }

  MachineBasicBlock::iterator Branch = MBB.getFirstTerminator();
  assert(IA64InstrInfo::isConditionalBranch(*Branch) && "Not the branch");
  DebugLoc DL = Branch->getDebugLoc();
  TII->emitCompare(MBB, Branch, DL, IA64CC::CondCode(Cond[0].getImm()), Cond[1],
                   Cond[2]);
  TII->removeBranch(MBB);

  // The registers live on the path that bypasses each side: the other side's
  // live-ins, or the join block's.
  auto LiveInto = [&](MachineBasicBlock *Side, bool Whole, LivePhysRegs &Live) {
    Live.init(*TRI);
    if (!Whole) {
      Live.addLiveIns(*Join);
      return;
    }
    Live.addLiveOuts(*Side);
    for (MachineInstr &MI : reverse(*Side))
      Live.stepBackward(MI);
  };
  LivePhysRegs NotT, NotF, JoinLive;
  LiveInto(FBB, QualifyF, NotT);
  LiveInto(TBB, QualifyT, NotF);
  if (Join) {
    JoinLive.init(*TRI);
    JoinLive.addLiveIns(*Join);
  }

  if (Returns)
    Return = TBB->back().removeFromParent();
  for (auto [Succ, Pred, Wanted, Otherwise] :
       {std::tuple(TBB, IA64::P6, QualifyT, &NotT),
        std::tuple(FBB, IA64::P7, QualifyF, &NotF)}) {
    if (!Wanted)
      continue;
    qualify(*Succ, MBB, Pred, *Otherwise, Join ? &JoinLive : nullptr, Early);
    if (Early)
      MBB.back().getOperand(1).setImm(MBPI->getEdgeProbability(&MBB, Succ) >=
                                              BranchProbability(1, 2)
                                          ? IA64II::WhetherDPTK
                                          : IA64II::WhetherDPNT);
    else if (Join)
      Succ->removeSuccessor(Join);
    MBB.removeSuccessor(Succ);
    Succ->eraseFromParent();
  }
  if (Return) {
    MBB.push_back(Return);
    return true;
  }
  if (MBB.succ_empty())
    MBB.addSuccessor(Join, BranchProbability::getOne());
  else
    MBB.setSuccProbability(MBB.succ_begin(), BranchProbability::getOne());
  if (!MBB.isLayoutSuccessor(Join))
    TII->insertBranch(MBB, Join, nullptr, {}, DL);
  return true;
}

// Merge MBB's single successor into it if nothing else reaches it, so that
// the result may become a side of a branch further up.
bool IA64IfConversion::mergeWithSuccessor(MachineBasicBlock &MBB) const {
  MachineBasicBlock *Succ = getOnlySuccessor(MBB);
  if (!Succ || Succ == &MBB || Succ->pred_size() != 1 || !isPlain(*Succ))
    return false;
  // Succ's layout fallthrough.
  MachineBasicBlock *Next = Succ->getFallThrough();
  TII->removeBranch(MBB);
  MBB.splice(MBB.end(), Succ, Succ->begin(), Succ->end());
  MBB.removeSuccessor(Succ);
  MBB.transferSuccessors(Succ);
  Succ->eraseFromParent();
  if (Next && !MBB.isLayoutSuccessor(Next))
    TII->insertBranch(MBB, Next, nullptr, {}, DebugLoc());
  return true;
}

bool IA64IfConversion::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();

  bool Changed = false;
  for (bool Again = true; Again;) {
    Again = false;
    // A block that grew may now be a side of the block before it.
    for (MachineBasicBlock &MBB : reverse(MF)) {
      if (!convert(MBB))
        continue;
      while (mergeWithSuccessor(MBB))
        ;
      Again = Changed = true;
      break;
    }
  }
  if (Changed)
    MF.RenumberBlocks();
  return Changed;
}

FunctionPass *llvm::createIA64IfConversionPass() {
  return new IA64IfConversion();
}
