//===-- IA64FrameLowering.cpp - IA64 Frame Lowering -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64FrameLowering.h"
#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
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

  // A call overwrites ar.pfs and b0, so a function with calls saves both and
  // needs a register stack frame to hold them.
  auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  bool SaveRP = MF.getFrameInfo().hasCalls();
  if (Locals != Inputs || Outputs != 0 || SaveRP) {
    Register SavedPFS = IA64::R32 + Locals++;
    FI->setSavedPFS(SavedPFS);
    if (SaveRP)
      FI->setSavedRP(IA64::R32 + Locals++);
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ALLOC), SavedPFS)
        .addImm(Inputs)
        .addImm(Locals - Inputs)
        .addImm(Outputs)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
    if (SaveRP) {
      BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_BR), FI->getSavedRP())
          .addReg(IA64::B0)
          .addReg(IA64::P0)
          .setMIFlag(MachineInstr::FrameSetup);
      MBB.addLiveIn(IA64::B0);
    }
    // Both are kept until the function returns, wherever it does.
    for (MachineBasicBlock &Other : llvm::drop_begin(MF)) {
      Other.addLiveIn(SavedPFS);
      if (SaveRP)
        Other.addLiveIn(FI->getSavedRP());
    }
  }

  // The 16 bytes at the incoming sp are the caller's scratch area, which
  // this function may use. So the frame starts 16 bytes above the incoming
  // sp and is that much smaller.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t Size = MFI.getStackSize();
  Size = Size > ScratchAreaSize ? alignTo(Size - ScratchAreaSize, 16) : 0;
  MFI.setStackSize(Size);
  adjustStackPointer(MBB, MBBI, DL, -int64_t(Size), MachineInstr::FrameSetup);
}

void IA64FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  adjustStackPointer(MBB, MBBI, DL, MF.getFrameInfo().getStackSize(),
                     MachineInstr::FrameDestroy);

  const auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  if (!FI->getSavedRP())
    return;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_BR), IA64::B0)
      .addReg(FI->getSavedRP(), RegState::Kill)
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlag(MachineInstr::FrameDestroy);
  BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_AR), IA64::AR64)
      .addReg(FI->getSavedPFS(), RegState::Kill)
      .addReg(IA64::P0)
      .setMIFlag(MachineInstr::FrameDestroy);
}

// Adjust sp in a single instruction, so it is never in an intermediate
// state. An amount too large for adds is materialized in r2 first.
void IA64FrameLowering::adjustStackPointer(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const DebugLoc &DL, int64_t Amount,
                                           MachineInstr::MIFlag Flag) const {
  if (Amount == 0)
    return;
  const IA64InstrInfo &TII = *static_cast<const IA64InstrInfo *>(
      MBB.getParent()->getSubtarget().getInstrInfo());
  if (isInt<14>(Amount)) {
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ADDS), IA64::R12)
        .addImm(Amount)
        .addReg(IA64::R12)
        .addReg(IA64::P0)
        .setMIFlag(Flag);
    return;
  }
  TII.loadImmediate(MBB, MBBI, DL, IA64::R2, Amount, Flag);
  BuildMI(MBB, MBBI, DL, TII.get(IA64::ADD), IA64::R12)
      .addReg(IA64::R2, RegState::Kill)
      .addReg(IA64::R12)
      .addReg(IA64::P0)
      .setMIFlag(Flag);
}

// Outgoing argument space is part of the fixed frame, so the call frame
// pseudos need no code.
MachineBasicBlock::iterator IA64FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  return MBB.erase(I);
}

StackOffset
IA64FrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = IA64::R12;
  return StackOffset::getFixed(MFI.getObjectOffset(FI) + ScratchAreaSize +
                               MFI.getStackSize());
}

bool IA64FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects();
}
