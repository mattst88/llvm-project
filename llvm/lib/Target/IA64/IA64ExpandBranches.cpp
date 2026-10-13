//===-- IA64ExpandBranches.cpp - Split branches from their compares -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A conditional branch is a compare that writes a predicate plus a branch
// predicated on it. They stay one instruction through block placement, since
// those passes analyze and reverse branch conditions, and this pass splits
// them afterwards.
//
// The branch's whether hint (dptk/dpnt) comes from the branch probability.
//
// Two conditions tested back to back with nothing in between become one
// branch. A parallel compare writes its predicates only when its relation
// holds, so the second compare accumulates its result into the predicate
// the first one wrote, and a single branch tests both.
//
// The pass also keeps a function from ending in a call. A call returns to
// the following bundle, and an unwinder attributes that address to the
// calling function, which would be wrong if the next function started
// there. A noreturn call may be the last instruction, so an unreachable
// break.f is appended after it.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-expand-branches"
#define PASS_NAME "IA64 branch expansion"

namespace {

class IA64ExpandBranches : public MachineFunctionPass {
public:
  static char ID;

  IA64ExpandBranches() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64ExpandBranches::ID = 0;

INITIALIZE_PASS_BEGIN(IA64ExpandBranches, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(IA64ExpandBranches, DEBUG_TYPE, PASS_NAME, false, false)

// The parallel compare (or.andcm) that sets its first predicate and clears
// its second if the condition holds, and leaves both unchanged otherwise.
// There are forms for equality against a register or immediate, and for
// comparing a register against zero, which covers orderings against zero
// and its neighbors 1 and -1. The operands after the predicates are
// appended to Operands.
static unsigned getParallelCompare(const MachineInstr &Branch,
                                   IA64CC::CondCode CC,
                                   SmallVectorImpl<MachineOperand> &Operands) {
  if (Branch.getOpcode() == IA64::BRCCf)
    return 0;
  Register LHS = Branch.getOperand(2).getReg();
  const MachineOperand &RHS = Branch.getOperand(3);
  if (CC == IA64CC::EQ || CC == IA64CC::NE) {
    bool Equal = CC == IA64CC::EQ;
    if (RHS.isImm()) {
      Operands.push_back(MachineOperand::CreateImm(RHS.getImm()));
      Operands.push_back(MachineOperand::CreateReg(LHS, false));
      return Equal ? IA64::CMP_EQ_OR_ANDCMi : IA64::CMP_NE_OR_ANDCMi;
    }
    Operands.push_back(MachineOperand::CreateReg(LHS, false));
    Operands.push_back(MachineOperand::CreateReg(RHS.getReg(), false));
    return Equal ? IA64::CMP_EQ_OR_ANDCM : IA64::CMP_NE_OR_ANDCM;
  }
  if (!RHS.isImm())
    return 0;
  // The instruction has zero on the left: "r < 0" is "0 > r".
  unsigned Opcode = 0;
  int64_t Imm = RHS.getImm();
  switch (CC) {
  default:
    return 0;
  case IA64CC::LT:
    Opcode = Imm == 0   ? IA64::CMP_GT_OR_ANDCMz
             : Imm == 1 ? IA64::CMP_GE_OR_ANDCMz
                        : 0;
    break;
  case IA64CC::LE:
    Opcode = Imm == 0    ? IA64::CMP_GE_OR_ANDCMz
             : Imm == -1 ? IA64::CMP_GT_OR_ANDCMz
                         : 0;
    break;
  case IA64CC::GT:
    Opcode = Imm == 0    ? IA64::CMP_LT_OR_ANDCMz
             : Imm == -1 ? IA64::CMP_LE_OR_ANDCMz
                         : 0;
    break;
  case IA64CC::GE:
    Opcode = Imm == 0   ? IA64::CMP_LE_OR_ANDCMz
             : Imm == 1 ? IA64::CMP_LT_OR_ANDCMz
                        : 0;
    break;
  }
  if (Opcode)
    Operands.push_back(MachineOperand::CreateReg(LHS, false));
  return Opcode;
}

// A block ending in a conditional branch that falls through to a block
// consisting only of another conditional branch, with no other predecessors,
// tests two conditions where one branch would do. If both branches go to the
// same target, the merged branch is taken if either condition holds. If the
// second block's fallthrough goes to the first branch's target, the merged
// branch is the second one, taken if the first condition is false and the
// second is true.
static bool mergeBranches(MachineBasicBlock &MBB, const IA64InstrInfo &TII,
                          const MachineBranchProbabilityInfo &MBPI) {
  MachineBasicBlock::iterator First = MBB.getFirstTerminator();
  if (First == MBB.end() || !IA64InstrInfo::isConditionalBranch(*First) ||
      std::next(First) != MBB.end() || MBB.succ_size() != 2)
    return false;
  MachineBasicBlock *Target = First->getOperand(0).getMBB();
  MachineFunction::iterator NextIt = std::next(MBB.getIterator());
  if (NextIt == MBB.getParent()->end())
    return false;
  MachineBasicBlock &Next = *NextIt;
  if (&Next == Target || !MBB.isSuccessor(&Next) || Next.pred_size() != 1 ||
      Next.succ_size() != 2 || Next.hasAddressTaken() || Next.isEHPad())
    return false;

  MachineBasicBlock::iterator Second = Next.getFirstNonDebugInstr();
  // An FP compare may raise an exception, which it must not do on a path
  // the first branch would have bypassed.
  if (Second == Next.end() || !IA64InstrInfo::isConditionalBranch(*Second) ||
      Second->getOpcode() == IA64::BRCCf)
    return false;
  MachineBasicBlock *NextTarget = Second->getOperand(0).getMBB();
  // Where the second block goes when its branch is not taken.
  MachineBasicBlock *Rest = nullptr;
  MachineBasicBlock::iterator After = next_nodbg(Second, Next.end());
  if (After == Next.end()) {
    MachineFunction::iterator Fallthrough = std::next(NextIt);
    if (Fallthrough == MBB.getParent()->end())
      return false;
    Rest = &*Fallthrough;
  } else if (After->getOpcode() == IA64::BRUNCOND &&
             next_nodbg(After, Next.end()) == Next.end()) {
    Rest = After->getOperand(0).getMBB();
  } else {
    return false;
  }
  if (Rest == NextTarget || !Next.isSuccessor(Rest) ||
      !Next.isSuccessor(NextTarget) || NextTarget == &Next)
    return false;

  bool Either = NextTarget == Target;
  if (!Either && Rest != Target)
    return false;

  // The first compare is a normal one, writing the predicate and its
  // complement, and the second is the parallel one. Either condition can go
  // first, since neither compare does anything but write predicates. For
  // the branch to be taken only if both conditions hold, the second compare
  // sets the complement predicate when its condition is false.
  IA64CC::CondCode CC1 = IA64CC::CondCode(First->getOperand(1).getImm());
  IA64CC::CondCode CC2 = IA64CC::CondCode(Second->getOperand(1).getImm());
  if (!Either)
    CC1 = IA64CC::getOpposite(CC1);
  MachineInstr *Plain = &*First, *Parallel = &*Second;
  IA64CC::CondCode PlainCC = CC1, ParallelCC = CC2;
  SmallVector<MachineOperand, 2> Operands;
  unsigned Opcode = getParallelCompare(
      *Parallel, Either ? ParallelCC : IA64CC::getOpposite(ParallelCC),
      Operands);
  if (!Opcode) {
    std::swap(Plain, Parallel);
    std::swap(PlainCC, ParallelCC);
    Operands.clear();
    Opcode = getParallelCompare(
        *Parallel, Either ? ParallelCC : IA64CC::getOpposite(ParallelCC),
        Operands);
    if (!Opcode)
      return false;
  }

  BranchProbability ToNext = MBPI.getEdgeProbability(&MBB, &Next);
  BranchProbability Taken = ToNext * MBPI.getEdgeProbability(&Next, NextTarget);
  if (Either)
    Taken = MBPI.getEdgeProbability(&MBB, Target) + Taken;

  DebugLoc DL = First->getDebugLoc();
  TII.emitCompare(MBB, MBB.end(), DL, PlainCC, Plain->getOperand(2),
                  Plain->getOperand(3));
  MachineInstrBuilder MIB =
      BuildMI(MBB, MBB.end(), DL, TII.get(Opcode))
          .addReg(Either ? IA64::P6 : IA64::P7, RegState::Define)
          .addReg(Either ? IA64::P7 : IA64::P6, RegState::Define);
  for (const MachineOperand &MO : Operands)
    MIB.add(MO);
  MIB.addReg(IA64::P0);
  BuildMI(MBB, MBB.end(), DL, TII.get(IA64::BR_COND))
      .addMBB(NextTarget)
      .addImm(Taken >= BranchProbability(1, 2) ? IA64II::WhetherDPTK
                                               : IA64II::WhetherDPNT)
      .addImm(0)
      .addImm(0)
      .addReg(IA64::P6);
  First->eraseFromParent();
  Second->eraseFromParent();

  if (Either) {
    MBB.setSuccProbability(find(MBB.successors(), Target), Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
  } else {
    MBB.removeSuccessor(Target);
    MBB.addSuccessor(NextTarget, Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
  }
  Next.removeSuccessor(NextTarget);
  Next.normalizeSuccProbs();

  // Further blocks that branch to the same target on their own conditions
  // are folded into the branch, each as another parallel compare.
  MachineBasicBlock *Empty = &Next;
  while (Either && Empty->getFirstNonDebugInstr() == Empty->end()) {
    MachineFunction::iterator MoreIt = std::next(Empty->getIterator());
    if (MoreIt == MBB.getParent()->end())
      break;
    MachineBasicBlock &More = *MoreIt;
    if (!Empty->isSuccessor(&More) || More.pred_size() != 1 ||
        More.succ_size() != 2 || More.hasAddressTaken() || More.isEHPad() ||
        &More == Target || !More.isSuccessor(Target))
      break;
    MachineBasicBlock::iterator Branch = More.getFirstNonDebugInstr();
    if (Branch == More.end() || !IA64InstrInfo::isConditionalBranch(*Branch) ||
        Branch->getOperand(0).getMBB() != Target)
      break;
    // Any remaining branch must go somewhere other than the target.
    MachineBasicBlock::iterator Tail = next_nodbg(Branch, More.end());
    if (Tail != More.end() && (Tail->getOpcode() != IA64::BRUNCOND ||
                               Tail->getOperand(0).getMBB() == Target ||
                               next_nodbg(Tail, More.end()) != More.end()))
      break;
    Operands.clear();
    Opcode = getParallelCompare(
        *Branch, IA64CC::CondCode(Branch->getOperand(1).getImm()), Operands);
    if (!Opcode)
      break;
    MachineBasicBlock::iterator Merged = MBB.getFirstTerminator();
    MachineInstrBuilder Compare = BuildMI(MBB, Merged, DL, TII.get(Opcode))
                                      .addReg(IA64::P6, RegState::Define)
                                      .addReg(IA64::P7, RegState::Define);
    for (const MachineOperand &MO : Operands)
      Compare.add(MO);
    Compare.addReg(IA64::P0);
    Taken += Taken.getCompl() * MBPI.getEdgeProbability(&More, Target);
    MBB.setSuccProbability(find(MBB.successors(), Target), Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
    Merged->getOperand(1).setImm(Taken >= BranchProbability(1, 2)
                                     ? IA64II::WhetherDPTK
                                     : IA64II::WhetherDPNT);
    Branch->eraseFromParent();
    More.removeSuccessor(Target);
    More.normalizeSuccProbs();
    Empty = &More;
  }
  return true;
}

bool IA64ExpandBranches::runOnMachineFunction(MachineFunction &MF) {
  const IA64InstrInfo &TII = *MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  const MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  bool Changed = false;

  if (MF.getTarget().getOptLevel() != CodeGenOptLevel::None)
    for (MachineBasicBlock &MBB : MF)
      Changed |= mergeBranches(MBB, TII, MBPI);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB.terminators())) {
      if (!IA64InstrInfo::isConditionalBranch(MI))
        continue;
      const DebugLoc &DL = MI.getDebugLoc();
      MachineBasicBlock *Target = MI.getOperand(0).getMBB();
      TII.emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(1).getImm()),
                      MI.getOperand(2), MI.getOperand(3));
      bool Taken =
          MBPI.getEdgeProbability(&MBB, Target) >= BranchProbability(1, 2);
      BuildMI(MBB, MI, DL, TII.get(IA64::BR_COND))
          .addMBB(Target)
          .addImm(Taken ? IA64II::WhetherDPTK : IA64II::WhetherDPNT)
          .addImm(0)
          .addImm(0)
          .addReg(IA64::P6);
      MI.eraseFromParent();
      Changed = true;
    }
  }

  for (MachineBasicBlock &MBB : reverse(MF)) {
    auto Last = MBB.getLastNonDebugInstr();
    while (Last != MBB.end() && Last->isMetaInstruction())
      Last = Last == MBB.begin() ? MBB.end() : prev_nodbg(Last, MBB.begin());
    if (Last == MBB.end())
      continue;
    if (Last->isCall() && !Last->isReturn()) {
      BuildMI(MF.back(), MF.back().end(), DebugLoc(), TII.get(IA64::BREAK_F))
          .addImm(0)
          .addReg(IA64::P0);
      Changed = true;
    }
    break;
  }
  return Changed;
}

FunctionPass *llvm::createIA64ExpandBranchesPass() {
  return new IA64ExpandBranches();
}
