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
#include "llvm/IR/DiagnosticInfo.h"

using namespace llvm;

// mov pr mask selecting p1-p5, plus the single bit that covers the rotating
// predicates p16-p63.
static constexpr int64_t PreservedPredicates = 0x1003e;

IA64FrameLowering::IA64FrameLowering(const IA64Subtarget &STI)
    : TargetFrameLowering(StackGrowsDown, Align(16), /*LocalAreaOffset=*/0,
                          Align(16)) {}

// On entry a function may only use the stacked registers holding its
// incoming arguments (the caller's outputs). Using more requires an alloc,
// which returns the caller's ar.pfs in a GR; a function that makes calls
// keeps it there. That GR is the first local the function does not use.
//
// Locals are r32-r119 here, and r120-r127 stand for the outputs, whatever
// physical registers those end up being. The register allocator does not
// use the last locals, so the prologue saves go after the locals the body
// uses. Only inline asm can name one of those last locals; the frame then
// extends up to it, and the saves go in unused locals below it.
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

  // A syscall-linkage function has all eight inputs, whatever its declared
  // arguments, and none of them may be reused.
  if (MF.getFunction().hasFnAttribute("syscall-linkage"))
    Inputs = 8;

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
  // So does a function whose inline asm clobbers them.
  bool SaveRP = MF.getFrameInfo().hasCalls() || !MRI.def_empty(IA64::B0);
  bool RestorePFS = SaveRP || !MRI.def_empty(IA64::AR64);
  // Spilling a GR puts its NaT bit in ar.unat, which is callee-saved.
  bool SaveUNAT = FI->spillsVarArgs() ||
                  any_of(MF.getFrameInfo().getCalleeSavedInfo(),
                         [](const CalleeSavedInfo &CS) {
                           return IA64::GRRegClass.contains(CS.getReg());
                         });
  // Objects in a realigned frame are addressed from sp, or from a base
  // pointer copy of it if sp moves.
  bool Realign = TRI.hasStackRealignment(MF);
  bool NeedsBP = Realign && MF.getFrameInfo().hasVarSizedObjects();
  bool SaveOthers = FI->changesPR() || FI->changesLC() || FI->getChangedBRs();
  if (Locals != Inputs || Outputs != 0 || RestorePFS || SaveUNAT ||
      SaveOthers || hasFP(MF)) {
    unsigned NumSaves = 1 + SaveRP + SaveUNAT + hasFP(MF) + NeedsBP +
                        FI->changesPR() + FI->changesLC() +
                        popcount(FI->getChangedBRs());
    SmallVector<Register, 12> Saves;
    unsigned Used = Locals;
    for (; Locals != 88 && Saves.size() != NumSaves; ++Locals)
      Saves.push_back(IA64::R32 + Locals);
    for (unsigned I = Inputs; I != Used && Saves.size() != NumSaves; ++I)
      if (!MRI.isPhysRegUsed(IA64::R32 + I, /*SkipRegMaskTest=*/true))
        Saves.push_back(IA64::R32 + I);
    // A register stack frame is limited to 96 registers, and there is no
    // other place to keep what a call would overwrite.
    if (Saves.size() != NumSaves) {
      const Function &F = MF.getFunction();
      F.getContext().diagnose(DiagnosticInfoUnsupported(
          F, "a function that leaves no local register for what its "
             "prologue saves is not supported on IA-64"));
      return;
    }
    unsigned NextSave = 0;
    auto TakeSave = [&] { return Saves[NextSave++]; };
    Register SavedPFS = TakeSave();
    FI->setSavedPFS(SavedPFS);
    FI->setRestoresPFS(RestorePFS);
    if (SaveRP)
      FI->setSavedRP(TakeSave());
    if (SaveUNAT)
      FI->setSavedUNAT(TakeSave());
    if (hasFP(MF))
      FI->setFramePointer(TakeSave());
    if (NeedsBP)
      FI->setBasePointer(TakeSave());
    if (FI->changesPR())
      FI->setSavedPR(TakeSave());
    if (FI->changesLC())
      FI->setSavedLC(TakeSave());
    for (unsigned I = 0; I != 5; ++I)
      if (FI->getChangedBRs() & (1u << I))
        FI->setSavedBR(I, TakeSave());
    // A returns_twice call's regmask preserves no locals, since the second
    // return sees them as last left. The prologue saves are never written
    // after the prologue, which is what the epilogue needs, so mark them
    // preserved.
    const uint32_t *ReturnsTwiceMask =
        static_cast<const IA64RegisterInfo &>(TRI)
            .getReturnsTwiceCallPreservedMask();
    uint32_t *KeepsFrameMask = nullptr;
    for (MachineBasicBlock &Block : MF)
      for (MachineInstr &MI : Block)
        for (MachineOperand &MO : MI.operands()) {
          if (!MO.isRegMask() || MO.getRegMask() != ReturnsTwiceMask)
            continue;
          if (!KeepsFrameMask) {
            KeepsFrameMask = MF.allocateRegMask();
            unsigned Words = MachineOperand::getRegMaskSize(TRI.getNumRegs());
            std::copy_n(ReturnsTwiceMask, Words, KeepsFrameMask);
            for (Register Reg : Saves)
              KeepsFrameMask[Reg.id() / 32] |= 1u << (Reg.id() % 32);
          }
          MO.setRegMask(KeepsFrameMask);
        }
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ALLOC), SavedPFS)
        .addImm(Inputs)
        .addImm(Locals - Inputs)
        .addImm(Outputs)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
    SmallVector<MachineInstr *, 6> Sinkable;
    if (SaveRP) {
      Sinkable.push_back(
          BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_BR), FI->getSavedRP())
              .addReg(IA64::B0)
              .addReg(IA64::P0)
              .setMIFlag(MachineInstr::FrameSetup));
      if (!MBB.isLiveIn(IA64::B0))
        MBB.addLiveIn(IA64::B0);
    }
    if (SaveUNAT)
      BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_AR), FI->getSavedUNAT())
          .addReg(IA64::AR36)
          .addReg(IA64::P0)
          .setMIFlag(MachineInstr::FrameSetup);
    if (FI->getSavedPR())
      BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_PR), FI->getSavedPR())
          .addReg(IA64::P0)
          .setMIFlag(MachineInstr::FrameSetup);
    if (FI->getSavedLC())
      Sinkable.push_back(
          BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_AR), FI->getSavedLC())
              .addReg(IA64::AR65)
              .addReg(IA64::P0)
              .setMIFlag(MachineInstr::FrameSetup));
    for (unsigned I = 0; I != 5; ++I) {
      if (!FI->getSavedBR(I))
        continue;
      Sinkable.push_back(
          BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_FROM_BR), FI->getSavedBR(I))
              .addReg(IA64::B1 + I)
              .addReg(IA64::P0)
              .setMIFlag(MachineInstr::FrameSetup));
      if (!MBB.isLiveIn(IA64::B1 + I))
        MBB.addLiveIn(IA64::B1 + I);
    }
    // The I-unit moves of ar.lc and the branch registers would compete for
    // the two I ports with the first instructions of the body, so each goes
    // as late in the block as it can: before the first instruction that
    // touches either register, or that a call, a label or an epilogue may
    // need it before. The unwind directives follow it there.
    for (MachineInstr *MI : Sinkable) {
      Register Save = MI->getOperand(0).getReg();
      Register From = MI->getOperand(1).getReg();
      MachineBasicBlock::iterator To = MBBI;
      while (
          To != MBB.end() && !To->isCall() && !To->isTerminator() &&
          !To->isLabel() && !To->isInlineAsm() &&
          !To->hasUnmodeledSideEffects() &&
          !To->getFlag(MachineInstr::FrameDestroy) &&
          !To->readsRegister(From, &TRI) && !To->modifiesRegister(From, &TRI) &&
          !To->readsRegister(Save, &TRI) && !To->modifiesRegister(Save, &TRI))
        ++To;
      MBB.splice(To, &MBB, MI);
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
      if (SaveUNAT)
        Other.addLiveIn(FI->getSavedUNAT());
      if (FI->getFramePointer())
        Other.addLiveIn(FI->getFramePointer());
      if (NeedsBP)
        Other.addLiveIn(FI->getBasePointer());
      if (FI->getSavedPR())
        Other.addLiveIn(FI->getSavedPR());
      if (FI->getSavedLC())
        Other.addLiveIn(FI->getSavedLC());
      for (unsigned I = 0; I != 5; ++I)
        if (FI->getSavedBR(I))
          Other.addLiveIn(FI->getSavedBR(I));
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
  // All GR fills are done by now, which was the last use of the NaT bits of
  // this function's spills.
  if (FI->getSavedUNAT())
    BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_AR), IA64::AR36)
        .addReg(FI->getSavedUNAT(), RegState::Kill)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  if (FI->getFramePointer())
    BuildMI(MBB, MBBI, DL, TII.get(IA64::ADDS), IA64::R12)
        .addImm(0)
        .addReg(FI->getFramePointer(), RegState::Kill)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  else
    adjustStackPointer(MBB, MBBI, DL, MF.getFrameInfo().getStackSize(),
                       MachineInstr::FrameDestroy);

  // Restore only the callee-saved predicates: the others may hold values
  // that later instructions depend on.
  if (FI->getSavedPR()) {
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_PR))
                                  .addReg(FI->getSavedPR(), RegState::Kill)
                                  .addImm(PreservedPredicates)
                                  .addReg(IA64::P0)
                                  .setMIFlag(MachineInstr::FrameDestroy);
    for (unsigned I = 1; I != 64; ++I)
      if (I <= 5 || I >= 16)
        MIB.addReg(IA64::P0 + I, RegState::ImplicitDefine);
  }
  if (FI->getSavedLC())
    BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_AR), IA64::AR65)
        .addReg(FI->getSavedLC(), RegState::Kill)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  for (unsigned I = 0; I != 5; ++I)
    if (FI->getSavedBR(I))
      BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_BR), IA64::B1 + I)
          .addReg(FI->getSavedBR(I), RegState::Kill)
          .addImm(0)
          .addReg(IA64::P0)
          .setMIFlag(MachineInstr::FrameDestroy);
  if (FI->getSavedRP())
    BuildMI(MBB, MBBI, DL, TII.get(IA64::MOV_BR), IA64::B0)
        .addReg(FI->getSavedRP(), RegState::Kill)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  if (!FI->restoresPFS())
    return;
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

// The branch registers, predicates and ar.lc are saved by the prologue in
// locals, like b0, so none of them gets a stack slot.
void IA64FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  auto *FI = MF.getInfo<IA64MachineFunctionInfo>();
  unsigned BRs = 0;
  for (unsigned I = 0; I != 5; ++I) {
    if (SavedRegs.test(IA64::B1 + I))
      BRs |= 1u << I;
    SavedRegs.reset(IA64::B1 + I);
  }
  FI->setChangedBRs(BRs);
  bool PR = false;
  for (unsigned I = 1; I != 64; ++I) {
    PR |= SavedRegs.test(IA64::P0 + I);
    SavedRegs.reset(IA64::P0 + I);
  }
  FI->setChangesPR(PR);
  FI->setChangesLC(SavedRegs.test(IA64::AR65));
  SavedRegs.reset(IA64::AR65);
}

// Outgoing argument space is part of the fixed frame, so the call frame
// pseudos need no code.
MachineBasicBlock::iterator IA64FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  return MBB.erase(I);
}

// Callee-saved registers are spilled in full, GRs with their NaT bit and FRs
// as all 82 bits, regardless of what this function keeps in them.
bool IA64FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL;
  for (const CalleeSavedInfo &CS : CSI) {
    MCRegister Reg = CS.getReg();
    unsigned Opc;
    if (IA64::GRRegClass.contains(Reg))
      Opc = IA64::ST8_SPILL;
    else if (IA64::FRRegClass.contains(Reg))
      Opc = IA64::STF_SPILL;
    else
      report_fatal_error("IA64 cannot save a register of this class yet");
    BuildMI(MBB, MI, DL, TII.get(IA64::ADDS), IA64::R2)
        .addImm(0)
        .addFrameIndex(CS.getFrameIdx())
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MI, DL, TII.get(Opc))
        .addReg(IA64::R2, RegState::Kill)
        .addReg(Reg, RegState::Kill)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}

bool IA64FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  for (const CalleeSavedInfo &CS : reverse(CSI)) {
    MCRegister Reg = CS.getReg();
    // A GR can hold its own fill address.
    bool IsGR = IA64::GRRegClass.contains(Reg);
    MCRegister Addr = IsGR ? Reg : MCRegister(IA64::R2);
    BuildMI(MBB, MI, DL, TII.get(IA64::ADDS), Addr)
        .addImm(0)
        .addFrameIndex(CS.getFrameIdx())
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MI, DL, TII.get(IsGR ? IA64::LD8_FILL : IA64::LDF_FILL), Reg)
        .addReg(Addr, RegState::Kill)
        .addImm(0)
        .addReg(IA64::P0)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
  return true;
}

StackOffset
IA64FrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Offset = MFI.getObjectOffset(FI) + ScratchAreaSize;
  const auto *FuncInfo = MF.getInfo<IA64MachineFunctionInfo>();
  // In a realigned frame only the caller's stack arguments are at fixed
  // offsets from the incoming sp; everything else is relative to the aligned
  // sp. Callee-save slots also use the incoming sp, since the unwinder looks
  // for them there.
  bool Realigned = MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  bool IsSave =
      MFI.isCalleeSavedInfoValid() &&
      any_of(MFI.getCalleeSavedInfo(),
             [&](const CalleeSavedInfo &CS) { return CS.getFrameIdx() == FI; });
  if (Realigned && !MFI.isFixedObjectIndex(FI) && !IsSave) {
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
