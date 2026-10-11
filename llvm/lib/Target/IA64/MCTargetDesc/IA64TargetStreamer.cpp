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
