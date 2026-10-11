//===-- IA64ELFObjectWriter.cpp - IA64 ELF writer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class IA64ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit IA64ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/true, OSABI, ELF::EM_IA_64,
                                /*HasRelocationAddend=*/true) {}

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override;
};

} // end anonymous namespace

unsigned IA64ELFObjectWriter::getRelocType(const MCFixup &Fixup,
                                           const MCValue &Target,
                                           bool IsPCRel) const {
  switch (Fixup.getKind()) {
  case FK_Data_4:
    return IsPCRel ? ELF::R_IA64_PCREL32LSB : ELF::R_IA64_DIR32LSB;
  case FK_Data_8:
    return IsPCRel ? ELF::R_IA64_PCREL64LSB : ELF::R_IA64_DIR64LSB;
  default:
    reportError(Fixup.getLoc(), "unsupported relocation type");
    return ELF::R_IA64_NONE;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createIA64ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<IA64ELFObjectWriter>(OSABI);
}
