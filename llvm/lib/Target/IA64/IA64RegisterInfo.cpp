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
#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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
  return CSR_IA64_Call_RegMask;
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

  // Memory is only addressed through a register, so spilling or reloading a
  // frame slot needs a register for the address. r2 is reserved for that.
  Reserved.set(IA64::R2);

  // ar.pfs, which calls and returns exchange with the current frame marker.
  Reserved.set(IA64::AR64);

  // The last two locals are kept back so that the prologue always has
  // somewhere to save ar.pfs and the return link.
  Reserved.set(IA64::R118);
  Reserved.set(IA64::R119);

  return Reserved;
}

bool IA64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  // The only instruction with a frame index is the add that forms the
  // address of the object.
  MachineInstr &MI = *II;
  assert(MI.getOpcode() == IA64::ADDS && "a frame index outside an add");
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const IA64InstrInfo &TII =
      *static_cast<const IA64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetFrameLowering &TFL = *MF.getSubtarget().getFrameLowering();

  Register FrameReg;
  int64_t Offset = TFL.getFrameIndexReference(
                          MF, MI.getOperand(FIOperandNum).getIndex(), FrameReg)
                       .getFixed() +
                   MI.getOperand(1).getImm();

  if (isInt<14>(Offset)) {
    MI.getOperand(1).setImm(Offset);
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*isDef=*/false);
    return false;
  }

  // Too far for the add: the register that is to hold the address holds the
  // offset first.
  Register Reg = MI.getOperand(0).getReg();
  const DebugLoc &DL = MI.getDebugLoc();
  TII.loadImmediate(MBB, II, DL, Reg, Offset);
  BuildMI(MBB, II, DL, TII.get(IA64::ADD), Reg)
      .addReg(Reg, RegState::Kill)
      .addReg(FrameReg)
      .addReg(IA64::P0);
  MI.eraseFromParent();
  return true;
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  if (Register FP = MF.getInfo<IA64MachineFunctionInfo>()->getFramePointer())
    return FP;
  return IA64::R12;
}
