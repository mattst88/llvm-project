//===-- IA64HoistFallThrough.cpp - Start fall-through code before a branch ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The scheduler orders each block on its own, so the instructions of a block
// that a conditional branch falls through to cannot issue before the branch,
// in the slots the end of the branch's block leaves empty, even where they
// depend on nothing near it. IA64HoistFallThrough moves the integer
// operations at the start of such a block, where it is the likelier way and
// the branch is its only way in, to the end of the block before, so that the
// scheduler that runs after it can overlap them with the branch's block.
//
// An operation moves only where it cannot fault or have any other effect
// than the registers it writes, and where those are dead where the branch
// goes.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-hoist-fall-through"

STATISTIC(NumHoisted, "Number of instructions moved above a branch");

static cl::opt<bool>
    EnableHoisting("ia64-fall-through-hoisting", cl::init(true), cl::Hidden,
                   cl::desc("Move the start of a fall-through block above "
                            "the branch to it"));

static cl::opt<unsigned>
    MaxHoisted("ia64-fall-through-hoisting-max", cl::init(4), cl::Hidden,
               cl::desc("The most instructions moved above a branch"));

namespace {

class IA64HoistFallThrough : public MachineFunctionPass {
public:
  static char ID;

  IA64HoistFallThrough() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "IA64 fall-through hoisting";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64HoistFallThrough::ID = 0;

INITIALIZE_PASS_BEGIN(IA64HoistFallThrough, DEBUG_TYPE,
                      "IA64 fall-through hoisting", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(IA64HoistFallThrough, DEBUG_TYPE,
                    "IA64 fall-through hoisting", false, false)

FunctionPass *llvm::createIA64HoistFallThroughPass() {
  return new IA64HoistFallThrough();
}

/// Whether \p MI does nothing but write general registers, and so may run
/// on a path that does not need it.
static bool isSpeculatable(const MachineInstr &MI) {
  if (MI.mayLoadOrStore() || MI.isCall() || MI.isBranch() ||
      MI.isTerminator() || MI.hasUnmodeledSideEffects() || MI.isInlineAsm() ||
      MI.isPosition() || MI.isDebugInstr() || MI.isPseudo() ||
      MI.getFlag(MachineInstr::FrameSetup) ||
      MI.getFlag(MachineInstr::FrameDestroy))
    return false;
  bool Defines = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isRegMask())
      return false;
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (MO.isImplicit() || !IA64::GRRegClass.contains(Reg) || Reg == IA64::R0 ||
        Reg == IA64::R12)
      return false;
    Defines = true;
  }
  return Defines;
}

namespace {

/// The cycle each register of a block is ready in, were each instruction
/// to issue as soon as what it reads is.
struct Readiness {
  const TargetSchedModel &SM;
  DenseMap<Register, unsigned> Ready;
  /// Nothing issues before the end of a call.
  unsigned Floor = 0;

  unsigned issue(const MachineInstr &MI) const {
    unsigned Issue = Floor;
    for (const MachineOperand &MO : MI.uses())
      if (MO.isReg() && MO.getReg())
        Issue = std::max(Issue, Ready.lookup(MO.getReg()));
    return Issue;
  }

  void add(const MachineInstr &MI, unsigned Issue) {
    if (MI.isCall()) {
      Floor = Issue + 1;
      return;
    }
    for (const MachineOperand &MO : MI.defs())
      if (MO.getReg())
        Ready[MO.getReg()] = Issue + SM.computeInstrLatency(&MI);
  }
};

} // end anonymous namespace

/// Move what it can of the start of \p Succ, which only \p MBB's conditional
/// branch falls through to, to the end of \p MBB. What moves must be able to
/// issue by the time the branch does, so that MBB takes no longer.
static bool hoist(MachineBasicBlock &MBB, MachineBasicBlock &Succ,
                  MachineBasicBlock &Target, const TargetRegisterInfo &TRI,
                  const TargetSchedModel &SM) {
  Readiness R{SM, {}};
  unsigned Branch = 0;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    unsigned Issue = R.issue(MI);
    if (MI.isTerminator())
      Branch = std::max(Branch, Issue);
    else
      R.add(MI, Issue);
  }
  // Where the branch goes, what MBB writes must be dead.
  LivePhysRegs AtTarget(TRI);
  AtTarget.addLiveIns(Target);
  // The terminators run after what moves.
  LiveRegUnits Terminators(TRI);
  for (const MachineInstr &MI : MBB.terminators())
    Terminators.accumulate(MI);
  // What Succ reads and writes before the instruction considered, which
  // stays behind.
  LiveRegUnits Read(TRI), Written(TRI);
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  SmallVector<MachineInstr *, 4> ToMove;
  unsigned Scanned = 0;
  for (MachineInstr &MI : Succ) {
    if (MI.isDebugInstr())
      continue;
    if (++Scanned > 16 || MI.isCall() || MI.isTerminator())
      break;
    bool Movable = isSpeculatable(MI);
    for (const MachineOperand &MO : MI.operands()) {
      if (!Movable)
        break;
      if (!MO.isReg() || !MO.getReg())
        continue;
      Register Reg = MO.getReg();
      if (!Written.available(Reg) || !Terminators.available(Reg))
        Movable = false;
      else if (MO.isDef() &&
               (!Read.available(Reg) || !AtTarget.available(MRI, Reg)))
        Movable = false;
    }
    unsigned Issue = Movable ? R.issue(MI) : 0;
    if (Movable && Issue <= Branch) {
      R.add(MI, Issue);
      ToMove.push_back(&MI);
      if (ToMove.size() == MaxHoisted)
        break;
      continue;
    }
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (MO.isDef())
        Written.addReg(MO.getReg());
      else
        Read.addReg(MO.getReg());
    }
  }
  if (ToMove.empty())
    return false;
  MachineBasicBlock::iterator Pos = MBB.getFirstTerminator();
  for (MachineInstr *MI : ToMove) {
    MI->clearKillInfo();
    MBB.splice(Pos, &Succ, MI);
  }
  NumHoisted += ToMove.size();
  recomputeLiveIns(Succ);
  return true;
}

bool IA64HoistFallThrough::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableHoisting || skipFunction(MF.getFunction()))
    return false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineLoopInfo &MLI =
      getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  TargetSchedModel SM;
  SM.init(&MF.getSubtarget());
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    if (TII.analyzeBranch(MBB, TBB, FBB, Cond) || !TBB || FBB || Cond.empty())
      continue;
    // A block of nothing but its branch, with one way in, may have it merged
    // into the branch before, as a parallel compare.
    if (MBB.getFirstNonDebugInstr() == MBB.getFirstTerminator() &&
        MBB.pred_size() == 1)
      continue;
    MachineBasicBlock *Succ = MBB.getFallThrough();
    if (!Succ || Succ == TBB || Succ->pred_size() != 1 || Succ->isEHPad() ||
        TBB->isEHPad() || MLI.getLoopFor(Succ) != MLI.getLoopFor(&MBB) ||
        MBPI.getEdgeProbability(&MBB, Succ) <= BranchProbability(1, 2))
      continue;
    Changed |= hoist(MBB, *Succ, *TBB, TRI, SM);
  }
  return Changed;
}
