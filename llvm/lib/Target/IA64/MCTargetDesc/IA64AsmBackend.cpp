//===-- IA64AsmBackend.cpp - IA64 assembler backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

namespace {

class IA64AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  explicit IA64AsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createIA64ELFObjectWriter(OSABI);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

} // end anonymous namespace

void IA64AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                const MCValue &Target, uint8_t *Data,
                                uint64_t Value, bool IsResolved) {
  if (!IsResolved) {
    Asm->getWriter().recordRelocation(F, Fixup, Target, Value);
    return;
  }
  // Only data has fixups so far.
  assert(Fixup.getKind() < FirstTargetFixupKind && "unknown fixup kind");
  unsigned Size = getFixupKindInfo(Fixup.getKind()).TargetSize / 8;
  for (unsigned I = 0; I != Size; ++I)
    Data[I] = static_cast<uint8_t>(Value >> (8 * I));
}

bool IA64AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                  const MCSubtargetInfo *STI) const {
  // Code can only start at a bundle boundary, so what precedes the first
  // whole bundle of the padding is not executable.
  OS.write_zeros(Count % IA64II::BundleBytes);
  // An MMI bundle of nop.m, nop.m and nop.i, without a stop.
  const uint64_t Nop = 0x8000000;
  const uint64_t Low = 0x08 | Nop << 5 | Nop << 46;
  const uint64_t High = Nop >> 18 | Nop << 23;
  for (uint64_t I = 0, E = Count / IA64II::BundleBytes; I != E; ++I) {
    support::endian::write<uint64_t>(OS, Low, llvm::endianness::little);
    support::endian::write<uint64_t>(OS, High, llvm::endianness::little);
  }
  return true;
}

MCAsmBackend *llvm::createIA64AsmBackend(const Target &T,
                                         const MCSubtargetInfo &STI,
                                         const MCRegisterInfo &MRI,
                                         const MCTargetOptions &Options) {
  return new IA64AsmBackend(
      MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS()));
}
