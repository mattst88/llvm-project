//===-- IA64HoistRestores.cpp - Restore b0 and ar.pfs in predecessors -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Only port I0 moves to b0, ar.pfs and ar.lc, so an epilogue that restores
// all three takes a cycle for each, and a short return block has nothing to
// overlap them with. When the other instructions of a return block take
// fewer cycles than its restores, IA64HoistRestores moves the restores of b0
// and ar.pfs into the blocks that branch to it, where the scheduler can put
// them in a group with an I port to spare.
//
// The moves are safe on the other paths from those blocks too. Only a return
// reads b0 and ar.pfs, and every way to a return restores them; a call
// writes both, and no one reads what it leaves there. Nor does the unwinder, which
// finds the caller's values in the registers they were saved to. ar.lc does
// not move: a counted loop on another path may still be using it.
//
// A block gets no restore where b0 or ar.pfs holds the caller's value
// already on every way to its end: no call has come since the prologue
// saved it, or since another restore. Branch expansion can then still fold
// a block holding only a branch into the one before it.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-hoist-restores"

STATISTIC(NumHoisted, "Number of restores moved to predecessors");

static cl::opt<bool>
    EnableHoisting("ia64-early-restores", cl::init(true), cl::Hidden,
                   cl::desc("Restore b0 and ar.pfs before the return block"));

namespace {

class IA64HoistRestores : public MachineFunctionPass {
public:
  static char ID;

  IA64HoistRestores() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "IA64 restore hoisting"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64HoistRestores::ID = 0;

INITIALIZE_PASS_BEGIN(IA64HoistRestores, DEBUG_TYPE, "IA64 restore hoisting",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(IA64HoistRestores, DEBUG_TYPE, "IA64 restore hoisting",
                    false, false)

FunctionPass *llvm::createIA64HoistRestoresPass() {
  return new IA64HoistRestores();
}

/// Whether \p MI is a restore of the epilogue that only port I0 executes.
static bool isI0Restore(const MachineInstr &MI) {
  return MI.getFlag(MachineInstr::FrameDestroy) &&
         (MI.getOpcode() == IA64::MOV_BR ||
          MI.getOpcode() == IA64::MOV_AR_RESTORE);
}

/// Whether \p MI restores b0 or ar.pfs as the epilogue does.
static bool isRestore(const MachineInstr &MI, const TargetInstrInfo &TII) {
  if (!isI0Restore(MI) || TII.isPredicated(MI))
    return false;
  Register Dst = MI.getOperand(0).getReg();
  return Dst == IA64::B0 || Dst == IA64::AR64;
}

/// Whether the instructions of \p MBB other than its I0-only restores take
/// fewer cycles than the restores, so that some of them would wait for port
/// I0 at its end.
static bool lacksRoom(const MachineBasicBlock &MBB,
                      const TargetSchedModel &SM) {
  unsigned Restores = 0, Cycles = 0;
  DenseMap<Register, unsigned> Ready;
  for (const MachineInstr &MI : MBB) {
    if (MI.isTerminator() || MI.isDebugInstr())
      break;
    if (isI0Restore(MI)) {
      ++Restores;
      continue;
    }
    unsigned Issue = 0;
    for (const MachineOperand &MO : MI.uses())
      if (MO.isReg() && MO.getReg())
        Issue = std::max(Issue, Ready.lookup(MO.getReg()));
    Cycles = std::max(Cycles, Issue + 1);
    for (const MachineOperand &MO : MI.defs())
      if (MO.getReg())
        Ready[MO.getReg()] = Issue + SM.computeInstrLatency(&MI);
  }
  return Cycles < Restores;
}

namespace {

/// Where b0 or ar.pfs already holds what the epilogue would restore to it.
struct Restored {
  Register Dst, Src;
  const TargetRegisterInfo &TRI;
  /// The answers so far, taken to be false while one is worked out, so
  /// that a loop does not hold it.
  DenseMap<const MachineBasicBlock *, bool> Known;

  /// Whether Dst holds it when \p MBB ends: MBB restores or saves it, or
  /// every block before MBB holds it and MBB does not write Dst or Src.
  bool atEnd(const MachineBasicBlock *MBB) {
    auto [It, New] = Known.try_emplace(MBB, false);
    if (!New || Known.size() > 16)
      return It->second;
    bool Holds = compute(MBB);
    Known[MBB] = Holds;
    return Holds;
  }

  bool compute(const MachineBasicBlock *MBB) {
    for (const MachineInstr &MI : reverse(*MBB)) {
      if (MI.isCall())
        return false;
      if (MI.modifiesRegister(Dst, &TRI))
        return MI.readsRegister(Src, &TRI);
      if (MI.modifiesRegister(Src, &TRI))
        return MI.readsRegister(Dst, &TRI);
    }
    return !MBB->pred_empty() && !MBB->isEHPad() &&
           all_of(MBB->predecessors(),
                  [&](const MachineBasicBlock *P) { return atEnd(P); });
  }
};

} // end anonymous namespace

bool IA64HoistRestores::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableHoisting || skipFunction(MF.getFunction()))
    return false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineLoopInfo &MLI =
      getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TargetSchedModel SM;
  SM.init(&MF.getSubtarget());
  // The restores of each block, and the blocks to move them to.
  MapVector<MachineBasicBlock *, SmallVector<MachineInstr *, 2>> ToMove;
  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.isReturnBlock() || MBB.pred_empty() || MBB.isEHPad() ||
        !lacksRoom(MBB, SM))
      continue;
    // Each predecessor must reach the end of its instructions before it
    // branches here, and must not run more often than the return does.
    bool Movable = all_of(MBB.predecessors(), [&](MachineBasicBlock *Pred) {
      return Pred != &MBB && MLI.getLoopFor(Pred) == MLI.getLoopFor(&MBB) &&
             all_of(Pred->terminators(), [](const MachineInstr &MI) {
               return MI.isBranch() && !MI.isReturn() && !MI.isIndirectBranch();
             });
    });
    if (!Movable)
      continue;
    // The restores that the block's earlier instructions do not depend on.
    SmallVector<Register, 4> Written;
    for (MachineInstr &MI : MBB) {
      if (MI.isTerminator() || MI.isCall())
        break;
      if (isRestore(MI, TII)) {
        Register Src = MI.getOperand(1).getReg();
        if (none_of(Written, [&](Register R) {
              return TRI.regsOverlap(R, Src) ||
                     TRI.regsOverlap(R, MI.getOperand(0).getReg());
            }))
          for (MachineBasicBlock *Pred : MBB.predecessors())
            ToMove[Pred].push_back(&MI);
        continue;
      }
      if (MI.readsRegister(IA64::B0, &TRI) ||
          MI.readsRegister(IA64::AR64, &TRI))
        break;
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.isDef())
          Written.push_back(MO.getReg());
    }
  }
  if (ToMove.empty())
    return false;

  // A block gets no restore where every way to it has one already, or still
  // holds what the prologue saved. The blocks before it come first, so that
  // what they get is known.
  SmallPtrSet<MachineInstr *, 4> Originals;
  for (MachineBasicBlock *Pred :
       ReversePostOrderTraversal<MachineFunction *>(&MF)) {
    auto It = ToMove.find(Pred);
    if (It == ToMove.end())
      continue;
    for (MachineInstr *MI : It->second) {
      Originals.insert(MI);
      Register Dst = MI->getOperand(0).getReg();
      Restored R{Dst, MI->getOperand(1).getReg(), TRI, {}};
      if (R.atEnd(Pred))
        continue;
      MachineInstr *Copy = MF.CloneMachineInstr(MI);
      Copy->clearFlag(MachineInstr::FrameDestroy);
      Copy->getOperand(1).setIsKill(false);
      Pred->insert(Pred->getFirstTerminator(), Copy);
    }
  }
  for (MachineInstr *MI : Originals)
    MI->eraseFromParent();
  NumHoisted += Originals.size();
  return true;
}
