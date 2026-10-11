//===-- IA64TargetStreamer.cpp - IA64 Target Streamer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64TargetStreamer.h"
#include "IA64BaseInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

IA64TargetStreamer::IA64TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

IA64TargetStreamer::~IA64TargetStreamer() = default;

void IA64TargetStreamer::emitStop() {}

void IA64TargetStreamer::emitTemplate(unsigned Index) {}

void IA64TargetStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {}

void IA64TargetStreamer::emitEndp() {}

void IA64TargetStreamer::emitPrologue(unsigned Mask, unsigned GRSave) {}

void IA64TargetStreamer::emitBody() {}

const char *IA64TargetStreamer::getUnwindRegName(UnwindReg Reg) {
  switch (Reg) {
  case UR_RP:
    return "rp";
  case UR_PFS:
    return "ar.pfs";
  case UR_PR:
    return "pr";
  case UR_UNAT:
    return "ar.unat";
  case UR_LC:
    return "ar.lc";
  case UR_FPSR:
    return "ar.fpsr";
  case UR_RNAT:
    return "ar.rnat";
  case UR_BSP:
    return "ar.bsp";
  case UR_BSPSTORE:
    return "ar.bspstore";
  case UR_PRIUNAT:
    return "@priunat";
  }
  llvm_unreachable("not a register that unwinding knows");
}

void IA64TargetStreamer::emitFFrame(uint64_t Size) {}

void IA64TargetStreamer::emitVFrame(unsigned GR) {}

void IA64TargetStreamer::emitVFrameSP(uint64_t Offset) {}

void IA64TargetStreamer::emitSpillBase(uint64_t Offset) {}

void IA64TargetStreamer::emitSave(UnwindReg Reg, unsigned GR) {}

void IA64TargetStreamer::emitSaveSP(UnwindReg Reg, uint64_t Offset) {}

void IA64TargetStreamer::emitSavePSP(UnwindReg Reg, uint64_t Offset) {}

void IA64TargetStreamer::emitAltRP(unsigned BR) {}

void IA64TargetStreamer::emitUnwABI(unsigned ABI, unsigned Context) {}

IA64TargetAsmStreamer::IA64TargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : IA64TargetStreamer(S), OS(OS) {}

void IA64TargetAsmStreamer::emitStop() { OS << "\t;;\n"; }

void IA64TargetAsmStreamer::emitTemplate(unsigned Index) {
  OS << "\t." << StringRef(IA64II::BundleTemplates[Index].Name).lower() << '\n';
}

void IA64TargetAsmStreamer::printEntries() {
  const MCAsmInfo *MAI = &getStreamer().getContext().getAsmInfo();
  ListSeparator LS;
  for (const MCSymbol *Entry : Entries) {
    OS << LS;
    Entry->print(OS, MAI);
  }
  OS << '\n';
}

void IA64TargetAsmStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {
  this->Entries.assign(Entries.begin(), Entries.end());
  OS << "\t.proc\t";
  printEntries();
}

void IA64TargetAsmStreamer::emitEndp() {
  OS << "\t.endp\t";
  printEntries();
}

void IA64TargetAsmStreamer::emitPrologue(unsigned Mask, unsigned GRSave) {
  OS << "\t.prologue";
  if (Mask != 0)
    OS << '\t' << Mask << ", " << GRSave;
  OS << '\n';
}

void IA64TargetAsmStreamer::emitBody() { OS << "\t.body\n"; }

void IA64TargetAsmStreamer::emitFFrame(uint64_t Size) {
  OS << "\t.fframe\t" << Size << '\n';
}

void IA64TargetAsmStreamer::emitVFrame(unsigned GR) {
  OS << "\t.vframe\tr" << GR << '\n';
}

void IA64TargetAsmStreamer::emitVFrameSP(uint64_t Offset) {
  OS << "\t.vframesp\t" << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSpillBase(uint64_t Offset) {
  OS << "\t.spill\t" << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSave(UnwindReg Reg, unsigned GR) {
  OS << "\t.save\t" << getUnwindRegName(Reg) << ", r" << GR << '\n';
}

void IA64TargetAsmStreamer::emitSaveSP(UnwindReg Reg, uint64_t Offset) {
  OS << "\t.savesp\t" << getUnwindRegName(Reg) << ", " << Offset << '\n';
}

void IA64TargetAsmStreamer::emitSavePSP(UnwindReg Reg, uint64_t Offset) {
  OS << "\t.savepsp\t" << getUnwindRegName(Reg) << ", " << Offset << '\n';
}

void IA64TargetAsmStreamer::emitAltRP(unsigned BR) {
  OS << "\t.altrp\tb" << BR << '\n';
}

void IA64TargetAsmStreamer::emitUnwABI(unsigned ABI, unsigned Context) {
  OS << "\t.unwabi\t" << ABI << ", " << Context << '\n';
}
