//===-- IA64HoistLoadUses.cpp - Read loaded values before stores ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// On Itanium 2, a store over what an integer load just read cancels the
// fill of L1D that the load started, unless an instruction that reads the
// loaded value issued in a group before the store's: that one waits for the
// load, and with it the fill. A cancelled fill leaves the line
// out of L1D, so a loop that loads a byte and stores over it each iteration
// (bzip2's move-to-front) takes 12 cycles an iteration where it could take
// under 5.
//
// IA64InsertStops starts the store's group after such a read. This pass
// provides the read where nothing between the load and the store has one:
// it moves the first instruction after the store that reads the loaded
// value and can go before the store, often the compare of the block's
// branch.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-hoist-load-uses"
#define PASS_NAME "IA64 load use hoisting"

STATISTIC(NumHoisted, "Number of loaded values read before a store");

static cl::opt<bool> EnableHoist("ia64-read-before-store", cl::Hidden,
                                 cl::init(true),
                                 cl::desc("Read a loaded value before a store "
                                          "that may overwrite it"));

namespace {

class IA64HoistLoadUses : public MachineFunctionPass {
public:
  static char ID;

  IA64HoistLoadUses() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64HoistLoadUses::ID = 0;

INITIALIZE_PASS_BEGIN(IA64HoistLoadUses, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(IA64HoistLoadUses, DEBUG_TYPE, PASS_NAME, false, false)

/// Whether \p A and \p B touch a register that either writes.
static bool conflicts(const MachineInstr &A, const MachineInstr &B,
                      const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MA : A.operands()) {
    if (!MA.isReg() || !MA.getReg())
      continue;
    for (const MachineOperand &MB : B.operands())
      if (MB.isReg() && MB.getReg() && (MA.isDef() || MB.isDef()) &&
          TRI.regsOverlap(MA.getReg(), MB.getReg()))
        return true;
  }
  return false;
}

/// Whether \p MI may move within its block past instructions it shares no
/// register with.
static bool isMovable(const MachineInstr &MI) {
  return !MI.isTerminator() && !MI.isCall() && !MI.isInlineAsm() &&
         !MI.mayLoadOrStore() && !MI.hasUnmodeledSideEffects() &&
         !MI.isMetaInstruction();
}

bool IA64HoistLoadUses::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableHoist || skipFunction(MF.getFunction()))
    return false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  AAResults *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &Store : MBB) {
      if (!Store.mayStore() || Store.isCall())
        continue;
      // The nearest load before the store that it may overwrite.
      MachineBasicBlock::iterator S = Store.getIterator();
      MachineInstr *Load = nullptr;
      for (MachineInstr &MI :
           make_range(std::next(S.getReverse()), MBB.rend())) {
        if (MI.isCall() || MI.isInlineAsm())
          break;
        if (IA64InstrInfo::cancelsFill(MI, Store, AA)) {
          Load = &MI;
          break;
        }
      }
      if (!Load)
        continue;
      // Nothing in between reads or replaces the loaded value.
      Register Reg = Load->getOperand(0).getReg();
      if (any_of(make_range(std::next(MachineBasicBlock::iterator(Load)), S),
                 [&](const MachineInstr &MI) {
                   return !MI.isMetaInstruction() &&
                          (MI.readsRegister(Reg, &TRI) ||
                           MI.modifiesRegister(Reg, &TRI));
                 }))
        continue;
      // The first reader after the store that can move before it.
      MachineInstr *Use = nullptr;
      for (MachineInstr &MI : make_range(std::next(S), MBB.end())) {
        if (MI.isMetaInstruction())
          continue;
        // A call's register mask clobbers registers that conflicts() does
        // not see.
        if (MI.isCall() || MI.isInlineAsm())
          break;
        if (MI.readsRegister(Reg, &TRI) && isMovable(MI) &&
            none_of(make_range(S, MachineBasicBlock::iterator(MI)),
                    [&](const MachineInstr &Between) {
                      return !Between.isMetaInstruction() &&
                             conflicts(Between, MI, TRI);
                    })) {
          Use = &MI;
          break;
        }
        if (MI.modifiesRegister(Reg, &TRI))
          break;
      }
      if (!Use)
        continue;
      MBB.splice(S, &MBB, Use);
      ++NumHoisted;
      Changed = true;
    }
  }
  return Changed;
}

FunctionPass *llvm::createIA64HoistLoadUsesPass() {
  return new IA64HoistLoadUses();
}
