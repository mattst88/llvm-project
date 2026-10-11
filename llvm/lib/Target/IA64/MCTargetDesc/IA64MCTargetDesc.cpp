//===-- IA64MCTargetDesc.cpp - IA64 target descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64MCTargetDesc.h"
#include "IA64ELFStreamer.h"
#include "IA64InstPrinter.h"
#include "IA64MCAsmInfo.h"
#include "IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#include "IA64GenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "IA64GenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "IA64GenSubtargetInfo.inc"

using namespace llvm;

static MCAsmInfo *createIA64MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  return new IA64MCAsmInfo(TT, Options);
}

unsigned IA64::getOpcodeForUnit(unsigned Opcode, IA64II::Unit Unit) {
  // The M, I, F and B forms of each pseudo-op.
  static const struct {
    unsigned Pseudo;
    unsigned Forms[4];
  } Table[] = {
      {IA64::NOP, {IA64::NOP_M, IA64::NOP_I, IA64::NOP_F, IA64::NOP_B}},
      // Older processors lack hint.b, and GNU as requires an option before
      // accepting "hint" as hint.b.
      {IA64::HINT, {IA64::HINT_M, IA64::HINT_I, IA64::HINT_F, 0}},
      {IA64::BREAK,
       {IA64::BREAK_M, IA64::BREAK_I, IA64::BREAK_F, IA64::BREAK_B}},
      {IA64::CHK_S, {IA64::CHK_S_M, IA64::CHK_S_I, 0, 0}},
      {IA64::MOV_AR, {IA64::MOV_M_AR, IA64::MOV_I_AR, 0, 0}},
      {IA64::MOV_ARi, {IA64::MOV_M_ARi, IA64::MOV_I_ARi, 0, 0}},
      {IA64::MOV_FROM_AR, {IA64::MOV_M_FROM_AR, IA64::MOV_I_FROM_AR, 0, 0}},
  };
  for (const auto &Entry : Table) {
    if (Entry.Pseudo != Opcode)
      continue;
    switch (Unit) {
    case IA64II::UnitM:
      return Entry.Forms[0];
    case IA64II::UnitI:
      return Entry.Forms[1];
    case IA64II::UnitF:
      return Entry.Forms[2];
    case IA64II::UnitB:
      return Entry.Forms[3];
    default:
      return 0;
    }
  }
  return Opcode;
}

/// The units that can read or write application register \p Number: the M
/// unit has the low ones and the I unit ar64 to ar111. Either can reach the
/// ones that are ignored, ar48 to ar63 and ar112 to ar127.
static unsigned getARUnits(unsigned Number) {
  if (Number < 48)
    return 1u << IA64II::UnitM;
  if (Number >= 64 && Number < 112)
    return 1u << IA64II::UnitI;
  return 1u << IA64II::UnitM | 1u << IA64II::UnitI;
}

unsigned IA64::getSlotUnits(const MCInst &MI, const MCInstrInfo &MCII,
                            const MCRegisterInfo &MRI) {
  unsigned Opcode = MI.getOpcode();
  switch (IA64II::Unit Unit = IA64II::getUnit(MCII.get(Opcode).TSFlags)) {
  case IA64II::UnitA:
    return 1u << IA64II::UnitM | 1u << IA64II::UnitI;
  case IA64II::UnitX:
    return 1u << IA64II::UnitL;
  case IA64II::UnitNone:
    break;
  default:
    return 1u << Unit;
  }

  unsigned Units = 0;
  for (IA64II::Unit Unit :
       {IA64II::UnitM, IA64II::UnitI, IA64II::UnitF, IA64II::UnitB})
    if (unsigned Form = getOpcodeForUnit(Opcode, Unit);
        Form != 0 && Form != Opcode)
      Units |= 1u << Unit;
  switch (Opcode) {
  case IA64::HINT:
    // The M-unit form has a 19-bit immediate where the others have 21 bits.
    if (MI.getOperand(0).isImm() && !isUInt<19>(MI.getOperand(0).getImm()))
      Units &= ~(1u << IA64II::UnitM);
    break;
  case IA64::MOV_AR:
  case IA64::MOV_ARi:
    Units &= getARUnits(MRI.getEncodingValue(MI.getOperand(0).getReg()));
    break;
  case IA64::MOV_FROM_AR:
    Units &= getARUnits(MRI.getEncodingValue(MI.getOperand(1).getReg()));
    break;
  }
  return Units;
}

unsigned IA64::getDefaultOpcode(const MCInst &MI, const MCInstrInfo &MCII,
                                const MCRegisterInfo &MRI) {
  unsigned Units = getSlotUnits(MI, MCII, MRI);
  for (IA64II::Unit Unit :
       {IA64II::UnitM, IA64II::UnitI, IA64II::UnitF, IA64II::UnitB})
    if (Units & 1u << Unit)
      return getOpcodeForUnit(MI.getOpcode(), Unit);
  return MI.getOpcode();
}

MCInstrInfo *llvm::createIA64MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitIA64MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createIA64MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitIA64MCRegisterInfo(X, IA64::B0);
  return X;
}

static MCSubtargetInfo *createIA64MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createIA64MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCInstPrinter *createIA64MCInstPrinter(const Triple &TT,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new IA64InstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *createIA64AsmTargetStreamer(MCStreamer &S,
                                                     formatted_raw_ostream &OS,
                                                     MCInstPrinter *InstPrint) {
  return new IA64TargetAsmStreamer(S, OS);
}

static MCTargetStreamer *
createIA64ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new IA64TargetELFStreamer(S, STI);
}

static MCTargetStreamer *createIA64NullTargetStreamer(MCStreamer &S) {
  return new IA64TargetStreamer(S);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64TargetMC() {
  Target &T = getTheIA64Target();
  TargetRegistry::RegisterMCAsmInfo(T, createIA64MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createIA64MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createIA64MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createIA64MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createIA64MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createIA64MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createIA64AsmBackend);
  TargetRegistry::RegisterELFStreamer(T, createIA64ELFStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(T,
                                               createIA64ObjectTargetStreamer);
  TargetRegistry::RegisterAsmTargetStreamer(T, createIA64AsmTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(T, createIA64NullTargetStreamer);
}
