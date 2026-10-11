//===-- IA64RegisterInfo.cpp - IA64 Register Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64RegisterInfo.h"
#include "IA64.h"
#include "IA64FrameLowering.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "IA64GenRegisterInfo.inc"

using namespace llvm;

IA64RegisterInfo::IA64RegisterInfo() : IA64GenRegisterInfo(/*RA=*/IA64::B0) {}

const MCPhysReg *
IA64RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_IA64_SaveList;
}

const uint32_t *
IA64RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const {
  return CSR_IA64_RegMask;
}

BitVector IA64RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // Constant registers.
  Reserved.set(IA64::R0);
  Reserved.set(IA64::F0);
  Reserved.set(IA64::F1);
  Reserved.set(IA64::P0);

  // Global pointer, stack pointer and thread pointer.
  Reserved.set(IA64::R1);
  Reserved.set(IA64::R12);
  Reserved.set(IA64::R13);

  // b0, which every call writes and every return reads. The prologue saves
  // it in a local, and it is restored only to return.
  Reserved.set(IA64::B0);

  // The stacked registers exist only once a function has allocated a register
  // stack frame, which nothing does yet.
  for (unsigned Reg = IA64::R32; Reg <= IA64::R127; ++Reg)
    Reserved.set(Reg);

  return Reserved;
}

bool IA64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  report_fatal_error("IA64 frame index elimination is not yet implemented");
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return IA64::R12;
}
