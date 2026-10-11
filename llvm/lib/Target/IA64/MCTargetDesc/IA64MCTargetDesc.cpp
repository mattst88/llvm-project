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
