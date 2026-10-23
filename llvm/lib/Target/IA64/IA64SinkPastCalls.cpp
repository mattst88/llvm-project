//===-- IA64SinkPastCalls.cpp - Sink FR definitions below calls -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An FR live across a call needs one of the callee-saved f2-f5 and f16-f31,
// which the prologue spills and the epilogue fills, or a spill slot of its
// own. A GR live across a call needs only a stacked local, which the register
// stack engine keeps for free while the stack fits.
//
// Selection places a transfer from a GR, or a load of an argument from the
// caller's frame, where the IR defines its value, so a double passed in a GR
// and used only after a call is moved into an FR before it. Barnes-Hut's
// gravsub keeps three such arguments, and a fourth passed on the stack, in
// callee-saved FRs around its call to sqrt.
//
// This pass moves such a setf, or a load from an immutable stack slot, to just
// before its first use when a call lies between them in the same block, so
// that its source rather than its result is live across the call.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-sink-past-calls"
#define PASS_NAME "IA64 sink FR definitions below calls"

STATISTIC(NumSunk, "Number of FR definitions sunk below a call");

static cl::opt<bool> EnableSink(
    "ia64-sink-fr-defs", cl::Hidden, cl::init(true),
    cl::desc("Move a setf or an argument load below a call that comes "
             "before its first use"));

namespace {

class IA64SinkPastCalls : public MachineFunctionPass {
  MachineRegisterInfo *MRI = nullptr;
  const MachineFrameInfo *MFI = nullptr;

public:
  static char ID;

  IA64SinkPastCalls() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  bool isCandidate(const MachineInstr &MI) const;
};

} // end anonymous namespace

char IA64SinkPastCalls::ID = 0;

INITIALIZE_PASS(IA64SinkPastCalls, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MI only defines an FR from virtual registers, and so computes the
// same value anywhere its operands are available.
bool IA64SinkPastCalls::isCandidate(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case IA64::SETF_D:
  case IA64::SETF_S:
  case IA64::SETF_SIG:
  case IA64::SETF_EXP:
    break;
  default: {
    // A load from the caller's frame, which nothing writes.
    if (!MI.mayLoad() || MI.mayStore() || MI.hasUnmodeledSideEffects() ||
        !MI.hasOneMemOperand())
      return false;
    const MachineMemOperand *MMO = *MI.memoperands_begin();
    const auto *PSV =
        dyn_cast_if_present<FixedStackPseudoSourceValue>(MMO->getPseudoValue());
    if (!PSV || !MFI->isImmutableObjectIndex(PSV->getFrameIndex()) ||
        MMO->isVolatile() || MMO->isAtomic())
      return false;
    break;
  }
  }
  if (MI.getNumExplicitDefs() != 1 || !MI.implicit_operands().empty())
    return false;
  Register Dst = MI.getOperand(0).getReg();
  if (!Dst.isVirtual() || !IA64::FRRegClass.hasSubClassEq(MRI->getRegClass(Dst)))
    return false;
  for (const MachineOperand &MO : MI.explicit_uses())
    if (MO.isReg() && !MO.getReg().isVirtual() && MO.getReg() != IA64::P0)
      return false;
  return true;
}

bool IA64SinkPastCalls::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableSink || skipFunction(MF.getFunction()))
    return false;
  MRI = &MF.getRegInfo();
  MFI = &MF.getFrameInfo();
  if (!MFI->hasCalls())
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    // Each instruction's position, and the number of calls before it.
    DenseMap<const MachineInstr *, std::pair<unsigned, unsigned>> Pos;
    unsigned Calls = 0, N = 0;
    for (const MachineInstr &MI : MBB) {
      Pos[&MI] = {N++, Calls};
      if (MI.isCall())
        ++Calls;
    }
    if (!Calls)
      continue;

    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      if (!isCandidate(MI))
        continue;
      Register Dst = MI.getOperand(0).getReg();
      // The first use, which must be in this block like every other.
      MachineInstr *First = nullptr;
      bool Local = true;
      for (MachineInstr &User : MRI->use_nodbg_instructions(Dst)) {
        if (User.getParent() != &MBB || User.isPHI()) {
          Local = false;
          break;
        }
        if (!First || Pos[&User].first < Pos[First].first)
          First = &User;
      }
      if (!Local || !First || Pos[First].second == Pos[&MI].second)
        continue;
      MBB.splice(First->getIterator(), &MBB, MI.getIterator());
      Pos[&MI] = {Pos[First].first, Pos[First].second};
      ++NumSunk;
      Changed = true;
    }
  }
  return Changed;
}

FunctionPass *llvm::createIA64SinkPastCallsPass() {
  return new IA64SinkPastCalls();
}
