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
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool SaveRP = MF.getFrameInfo().hasCalls();
  // Objects in a realigned frame are addressed from sp, or from a base
  // pointer copy of it if sp moves.
  bool Realign = TRI.hasStackRealignment(MF);
  bool NeedsBP = Realign && MF.getFrameInfo().hasVarSizedObjects();
  if (Locals != Inputs || Outputs != 0 || SaveRP || hasFP(MF)) {
    Register SavedPFS = IA64::R32 + Locals++;
    FI->setSavedPFS(SavedPFS);
    if (SaveRP)
      FI->setSavedRP(IA64::R32 + Locals++);
    if (hasFP(MF))
      FI->setFramePointer(IA64::R32 + Locals++);
    if (NeedsBP)
      FI->setBasePointer(IA64::R32 + Locals++);
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
      if (!MBB.isLiveIn(IA64::B0))
        MBB.addLiveIn(IA64::B0);
    }
    // If sp moves in the body, the frame is located from the incoming sp.
    if (FI->getFramePointer())
      BuildMI(MBB, MBBI, DL, TII.get(IA64::ADDS), FI->getFramePointer())
          .addImm(0)
          .addReg(IA64::R12)
          .addReg(IA64::P0)
          .setMIFlag(MachineInstr::FrameSetup);
    // The saves are live until the function returns, from any block.
    for (MachineBasicBlock &Other : llvm::drop_begin(MF)) {
      Other.addLiveIn(SavedPFS);
      if (SaveRP)
        Other.addLiveIn(FI->getSavedRP());
      if (FI->getFramePointer())
        Other.addLiveIn(FI->getFramePointer());
      if (NeedsBP)
        Other.addLiveIn(FI->getBasePointer());
    }
  }

  // The 16 bytes at the incoming sp are the caller's scratch area, which
  // this function may use. So the frame starts 16 bytes above the incoming
  // sp and is that much smaller.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t Size = MFI.getStackSize();
  Size = Size > ScratchAreaSize ? alignTo(Size - ScratchAreaSize, 16) : 0;
  // Object offsets are relative to the end of the scratch area, so that is
  // the point to align when realigning sp. A function asked to realign a
  // stack its objects do not need still owes its callees the ABI alignment.
  Align MaxAlign = std::max(MFI.getMaxAlign(), getStackAlign());
  if (Realign)
    Size = alignTo(Size + ScratchAreaSize, MaxAlign) - ScratchAreaSize;
  MFI.setStackSize(Size);
  adjustStackPointer(MBB, MBBI, DL, -int64_t(Size), MachineInstr::FrameSetup);
  if (!Realign)
    return;

  int64_t Mask = -int64_t(MaxAlign.value());
  if (isInt<8>(Mask)) {
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ANDi), IA64::R12)
        .addImm(Mask)
        .addReg(IA64::R12)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
  } else {
    static_cast<const IA64InstrInfo &>(TII).loadImmediate(
        MBB, MBBI, DL, IA64::R2, Mask, MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(IA64::AND), IA64::R12)
        .addReg(IA64::R2, RegState::Kill)
        .addReg(IA64::R12)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  if (NeedsBP)
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ADDS), FI->getBasePointer())
        .addImm(0)
        .addReg(IA64::R12)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
}

void IA64FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  if (FI->getFramePointer())
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ADDS), IA64::R12)
        .addImm(0)
        .addReg(FI->getFramePointer(), RegState::Kill)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  else
    adjustStackPointer(MBB, MBBI, DL, MF.getFrameInfo().getStackSize(),
                       MachineInstr::FrameDestroy);

  if (!FI->getSavedRP())
    return;
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
  int64_t Offset = MFI.getObjectOffset(FI) + ScratchAreaSize;
  const auto *FuncInfo = MF.getInfo<IA64MachineFunctionInfo>();
  // Only what the caller put on the stack is where the stack pointer was
  // when it has since been aligned: the rest is where the stack pointer is.
  bool Realigned = MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  if (Realigned && !MFI.isFixedObjectIndex(FI)) {
    FrameReg = FuncInfo->getBasePointer();
    if (!FrameReg)
      FrameReg = IA64::R12;
    return StackOffset::getFixed(Offset + MFI.getStackSize());
  }
  FrameReg = FuncInfo->getFramePointer();
  if (FrameReg)
    return StackOffset::getFixed(Offset);
  FrameReg = IA64::R12;
  return StackOffset::getFixed(Offset + MFI.getStackSize());
}

// Outgoing argument space stays at sp when sp moves; dynamic allocations go
// above it.
bool IA64FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return true;
}

bool IA64FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
         MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}
