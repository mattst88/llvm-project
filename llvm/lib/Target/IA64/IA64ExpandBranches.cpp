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
    AU.setPreservesCFG();
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

bool IA64ExpandBranches::runOnMachineFunction(MachineFunction &MF) {
  const IA64InstrInfo &TII = *MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  const MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB.terminators())) {
      if (!IA64InstrInfo::isConditionalBranch(MI))
        continue;
      const DebugLoc &DL = MI.getDebugLoc();
      MachineBasicBlock *Target = MI.getOperand(0).getMBB();
      TII.emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(1).getImm()),
                      MI.getOperand(2).getReg(), MI.getOperand(3));
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
