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

const uint32_t *IA64RegisterInfo::getReturnsTwiceCallPreservedMask() const {
  return CSR_IA64_ReturnsTwice_RegMask;
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

  // ar.fpsr: the rounding mode and the raised exception flags.
  Reserved.set(IA64::AR40);

  // ar.lc, which counted loops set in the preheader and br.cloop counts down.
  // It is callee-saved, and the prologue saves it when anything writes it.
  Reserved.set(IA64::AR65);

  // ar.ec, which only software-pipelined loops use.
  Reserved.set(IA64::AR66);

  // The rotating registers that software-pipelined loops use. Their names
  // change as the loop branch rotates them, so they hold nothing else.
  auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  if (FI->rotates()) {
    for (unsigned I = 0; I != FI->getRotatingGRs(); ++I)
      Reserved.set(IA64::R32 + I);
    for (unsigned I = 0; I != FI->getRotatingFRs(); ++I)
      Reserved.set(IA64::F32 + I);
    for (unsigned Reg = IA64::P16; Reg <= IA64::P63; ++Reg)
      Reserved.set(Reg);
  }

  // RSE state: ar.rsc, ar.bsp, ar.bspstore and ar.rnat.
  Reserved.set(IA64::AR16);
  Reserved.set(IA64::AR17);
  Reserved.set(IA64::AR18);
  Reserved.set(IA64::AR19);

  // The last five locals are reserved so the prologue always has room to
  // save ar.pfs, b0 and ar.unat, and to hold the frame pointer and base
  // pointer. Inline asm may also clobber the predicates, ar.lc and b1-b5,
  // which need seven more. Inline asm can also name one of these locals
  // itself, and then the prologue looks below it for locals the body does
  // not use.
  unsigned FirstKept = MF.hasInlineAsm() ? IA64::R108 : IA64::R115;
  for (unsigned Reg = FirstKept; Reg <= IA64::R119; ++Reg)
    Reserved.set(Reg);

  // The kernel may restart a syscall with the arguments left in its input
  // registers, so a syscall-linkage function must not modify them.
  if (MF.getFunction().hasFnAttribute("syscall-linkage"))
    for (unsigned Reg = IA64::R32; Reg <= IA64::R39; ++Reg)
      Reserved.set(Reg);

  // Registers reserved by the user.
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  for (unsigned Reg = IA64::R2; Reg <= IA64::R31; ++Reg)
    if (STI.isRegisterReservedByUser(Reg))
      Reserved.set(Reg);
  for (unsigned Reg = IA64::F2; Reg <= IA64::F127; ++Reg)
    if (STI.isRegisterReservedByUser(Reg))
      Reserved.set(Reg);
  for (unsigned Reg = IA64::B1; Reg <= IA64::B5; ++Reg)
    if (STI.isRegisterReservedByUser(Reg))
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
  case IA64::SPILL_PR:
    // Zero, then under the predicate the address, which is not zero.
    FormAddress(IA64::R2);
    BuildMI(MBB, II, DL, TII.get(IA64::ST8))
        .addReg(IA64::R2)
        .addReg(IA64::R0)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
    BuildMI(MBB, II, DL, TII.get(IA64::ST8))
        .addReg(IA64::R2)
        .addReg(IA64::R2)
        .addImm(0)
        .add(MI.getOperand(0))
        .setMIFlag(Flags);
    break;
  case IA64::RELOAD_PR:
    FormAddress(IA64::R2);
    BuildMI(MBB, II, DL, TII.get(IA64::LD8), IA64::R2)
        .addReg(IA64::R2, RegState::Kill)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
    BuildMI(MBB, II, DL, TII.get(IA64::CMP_EQ))
        .addReg(IA64::P0, RegState::Define)
        .add(MI.getOperand(0))
        .addReg(IA64::R0)
        .addReg(IA64::R2, RegState::Kill)
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

// A call writes r1, but the call pseudo itself restores this function's GP,
// as does the copy after a returns_twice call and at a landing pad. So r1 holds
// GP wherever it is read, and an address formed from it is loop invariant.
bool IA64RegisterInfo::isCallerPreservedPhysReg(
    MCRegister PhysReg, const MachineFunction &MF) const {
  return PhysReg == IA64::R1;
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  if (Register FP = MF.getInfo<IA64MachineFunctionInfo>()->getFramePointer())
    return FP;
  return IA64::R12;
}
