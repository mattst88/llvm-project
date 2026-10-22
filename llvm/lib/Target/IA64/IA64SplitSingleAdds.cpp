//===-- IA64SplitSingleAdds.cpp - Add singles in double precision ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// On the Itanium 9340 a single-precision fadd.s or fsub.s stalls the whole
// pipeline for 3 cycles when its addend is below 2 in magnitude and its other
// operand has an exponent from 2 below the addend's to 1 above it. The same
// add in double precision never stalls, and neither does fnorm.s.
//
// Adding two singles in double precision and rounding the sum to single gives
// the correctly rounded single sum, as 53 >= 2 * 24 + 2, in every rounding
// mode, with the same exception flags: a sum that is tiny as a single is
// exact in both. So in a loop this pass turns
//
//   fadd.s r = a, b
//
// into
//
//   fadd.d t = a, b
//   fnorm.s r = t
//
// It costs a second F-unit instruction and 4 cycles of latency, so it leaves
// an add on a recurrence through the loop alone: a running sum of values
// that are all of a size never stalls, but would take twice as long.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-split-single-adds"
#define PASS_NAME "IA64 add singles in double precision"

STATISTIC(NumSplit, "Number of single-precision adds made double");
STATISTIC(NumRecurrence, "Number of single-precision adds left on a "
                         "recurrence");

static cl::opt<bool> EnableSplit(
    "ia64-single-adds-in-double", cl::Hidden, cl::init(true),
    cl::desc("Add singles in loops in double precision and round the sum "
             "to single"));

namespace {

class IA64SplitSingleAdds : public MachineFunctionPass {
  MachineRegisterInfo *MRI = nullptr;
  const TargetInstrInfo *TII = nullptr;

public:
  static char ID;

  IA64SplitSingleAdds() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  bool onRecurrence(const MachineInstr &MI, const MachineLoop *L) const;
};

} // end anonymous namespace

char IA64SplitSingleAdds::ID = 0;

INITIALIZE_PASS_BEGIN(IA64SplitSingleAdds, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(IA64SplitSingleAdds, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MI's result reaches one of its own operands through instructions
// of L, around L's back edge.
bool IA64SplitSingleAdds::onRecurrence(const MachineInstr &MI,
                                       const MachineLoop *L) const {
  SmallVector<const MachineInstr *, 16> Worklist{&MI};
  SmallPtrSet<const MachineInstr *, 16> Seen{&MI};
  while (!Worklist.empty()) {
    const MachineInstr *Def = Worklist.pop_back_val();
    for (const MachineOperand &MO : Def->defs()) {
      if (!MO.getReg().isVirtual())
        continue;
      for (const MachineInstr &User :
           MRI->use_nodbg_instructions(MO.getReg())) {
        if (&User == &MI)
          return true;
        if (L->contains(User.getParent()) && Seen.insert(&User).second)
          Worklist.push_back(&User);
      }
    }
  }
  return false;
}

bool IA64SplitSingleAdds::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableSplit || skipFunction(MF.getFunction()))
    return false;
  MRI = &MF.getRegInfo();
  TII = MF.getSubtarget().getInstrInfo();
  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineLoop *L = MLI.getLoopFor(&MBB);
    if (!L)
      continue;
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      // fadd.s and fsub.s: fma.s and fms.s with f1 for the multiplier,
      // unpredicated.
      unsigned Opc = MI.getOpcode();
      if ((Opc != IA64::FMA_S_SUM && Opc != IA64::FMS_S_SUM) ||
          MI.getOperand(2).getReg() != IA64::F1 ||
          MI.getOperand(5).getReg() != IA64::P0 ||
          !MI.getOperand(0).getReg().isVirtual())
        continue;
      if (onRecurrence(MI, L)) {
        ++NumRecurrence;
        continue;
      }
      Register Dst = MI.getOperand(0).getReg();
      Register Tmp = MRI->createVirtualRegister(MRI->getRegClass(Dst));
      MI.setDesc(TII->get(Opc == IA64::FMA_S_SUM ? IA64::FMA_D_SUM
                                                 : IA64::FMS_D_SUM));
      MI.getOperand(0).setReg(Tmp);
      BuildMI(MBB, std::next(MI.getIterator()), MI.getDebugLoc(),
              TII->get(IA64::FMA_S), Dst)
          .addReg(Tmp, RegState::Kill)
          .addReg(IA64::F1)
          .addReg(IA64::F0)
          .addImm(MI.getOperand(4).getImm())
          .addReg(IA64::P0)
          ->setFlags(MI.getFlags());
      ++NumSplit;
      Changed = true;
    }
  }
  return Changed;
}

FunctionPass *llvm::createIA64SplitSingleAddsPass() {
  return new IA64SplitSingleAdds();
}
