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

  // ar.unat, the NaT bits of spilled GRs.
  Reserved.set(IA64::AR36);

  // The last five locals are kept back so that the prologue always has
  // somewhere to save ar.pfs, the return link and ar.unat, and to keep the
  // stack pointer as it was on entry and as the prologue left it.
  for (unsigned Reg = IA64::R115; Reg <= IA64::R119; ++Reg)
    Reserved.set(Reg);

  return Reserved;
}

bool IA64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const IA64InstrInfo &TII =
      *static_cast<const IA64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetFrameLowering &TFL = *MF.getSubtarget().getFrameLowering();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineInstr::MIFlag Flags =
      MI.getFlag(MachineInstr::FrameSetup)     ? MachineInstr::FrameSetup
      : MI.getFlag(MachineInstr::FrameDestroy) ? MachineInstr::FrameDestroy
                                               : MachineInstr::NoFlags;

  Register FrameReg;
  int64_t Offset = TFL.getFrameIndexReference(
                          MF, MI.getOperand(FIOperandNum).getIndex(), FrameReg)
                       .getFixed();

  // Form the address in Reg, materializing the offset there first if it is
  // out of range for adds.
  auto FormAddress = [&](Register Reg) {
    if (isInt<14>(Offset)) {
      BuildMI(MBB, II, DL, TII.get(IA64::ADDS), Reg)
          .addImm(Offset)
          .addReg(FrameReg)
          .addReg(IA64::P0)
          .setMIFlag(Flags);
      return;
    }
    TII.loadImmediate(MBB, II, DL, Reg, Offset, Flags);
    BuildMI(MBB, II, DL, TII.get(IA64::ADD), Reg)
        .addReg(Reg, RegState::Kill)
        .addReg(FrameReg)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
  };

  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("a frame index in an instruction that takes none");
  case IA64::ADDS:
    Offset += MI.getOperand(1).getImm();
    if (isInt<14>(Offset)) {
      MI.getOperand(1).setImm(Offset);
      MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*isDef=*/false);
      return false;
    }
    FormAddress(MI.getOperand(0).getReg());
    break;
  case IA64::SPILL_GR:
  case IA64::SPILL_FR:
    FormAddress(IA64::R2);
    BuildMI(
        MBB, II, DL,
        TII.get(MI.getOpcode() == IA64::SPILL_GR ? IA64::ST8 : IA64::STF_SPILL))
        .addReg(IA64::R2, RegState::Kill)
        .add(MI.getOperand(0))
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
    break;
  case IA64::RELOAD_GR:
  case IA64::RELOAD_FR: {
    // A GR being reloaded holds its own address first, which keeps reloads
    // independent of each other.
    bool IsGR = MI.getOpcode() == IA64::RELOAD_GR;
    Register Reg = MI.getOperand(0).getReg();
    Register Addr = IsGR ? Reg : Register(IA64::R2);
    FormAddress(Addr);
    BuildMI(MBB, II, DL, TII.get(IsGR ? IA64::LD8 : IA64::LDF_FILL), Reg)
        .addReg(Addr, RegState::Kill)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
    break;
  }
  }
  MI.eraseFromParent();
  return true;
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  if (Register FP = MF.getInfo<IA64MachineFunctionInfo>()->getFramePointer())
    return FP;
  return IA64::R12;
}
