//===-- IA64FrameLowering.cpp - IA64 Frame Lowering -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64FrameLowering.h"
#include "IA64Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

IA64FrameLowering::IA64FrameLowering(const IA64Subtarget &STI)
    : TargetFrameLowering(StackGrowsDown, Align(16), /*LocalAreaOffset=*/0,
                          Align(16)) {}

// On entry a function may only use the stacked registers holding its
// incoming arguments (the caller's outputs). Using more requires an alloc,
// which returns the caller's ar.pfs in a GR; a function that makes calls
// keeps it there. That GR is the first local the function does not use.
void IA64FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  unsigned Inputs = 0;
  for (auto [Reg, VReg] : MRI.liveins())
    if (Reg >= IA64::R32 && Reg <= IA64::R39)
      Inputs = std::max(Inputs, Reg - IA64::R32 + 1);

  // Skip the regmask test: a call clobbering the outputs does not make this
  // function use them.
  unsigned Locals = Inputs, Outputs = 0;
  for (unsigned I = 0; I != 88; ++I)
    if (MRI.isPhysRegUsed(IA64::R32 + I, /*SkipRegMaskTest=*/true))
      Locals = std::max(Locals, I + 1);
  for (unsigned I = 0; I != 8; ++I)
    if (MRI.isPhysRegUsed(IA64::R120 + I, /*SkipRegMaskTest=*/true))
      Outputs = I + 1;

  if (Locals == Inputs && Outputs == 0)
    return;

  Register SavedPFS = IA64::R32 + Locals++;
  BuildMI(MBB, MBBI, DL, TII.get(IA64::ALLOC), SavedPFS)
      .addImm(Inputs)
      .addImm(Locals - Inputs)
      .addImm(Outputs)
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlag(MachineInstr::FrameSetup);
}

void IA64FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {}

bool IA64FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects();
}
