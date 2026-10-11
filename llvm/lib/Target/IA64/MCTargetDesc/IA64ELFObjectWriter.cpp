//===-- IA64ELFObjectWriter.cpp - IA64 ELF writer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64FixupKinds.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbolELF.h"
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
  bool needsRelocateWithSymbol(const MCValue &V, unsigned Type) const override;
};

} // end anonymous namespace

namespace {
// The relocation for each specifier in one fixup kind, in IA64::Specifier
// order. R_IA64_NONE marks an unsupported pair.
struct RelocRow {
  unsigned Kind;
  unsigned Types[IA64::S_LTOFF_DTPREL + 1];
};
} // end anonymous namespace

#define R(Name) ELF::R_IA64_##Name
static const RelocRow RelocTable[] = {
    // none, gprel, ltoff, ltoffx, pltoff, pcrel, fptr, secrel, segrel, ltv,
    // tprel, dtpmod, dtprel, ltoff(fptr), ltoff(tprel), ltoff(dtpmod),
    // ltoff(dtprel)
    {IA64::fixup_ia64_imm14,
     {R(IMM14), R(NONE), R(NONE), R(NONE), R(NONE), R(NONE), R(NONE), R(NONE),
      R(NONE), R(NONE), R(TPREL14), R(NONE), R(DTPREL14), R(NONE), R(NONE),
      R(NONE), R(NONE)}},
    {IA64::fixup_ia64_imm22,
     {R(IMM22), R(GPREL22), R(LTOFF22), R(LTOFF22X), R(PLTOFF22), R(PCREL22),
      R(NONE), R(NONE), R(NONE), R(NONE), R(TPREL22), R(NONE), R(DTPREL22),
      R(LTOFF_FPTR22), R(LTOFF_TPREL22), R(LTOFF_DTPMOD22), R(LTOFF_DTPREL22)}},
    {IA64::fixup_ia64_imm64,
     {R(IMM64), R(GPREL64I), R(LTOFF64I), R(NONE), R(PLTOFF64I), R(PCREL64I),
      R(FPTR64I), R(NONE), R(NONE), R(NONE), R(TPREL64I), R(NONE), R(DTPREL64I),
      R(LTOFF_FPTR64I), R(NONE), R(NONE), R(NONE)}},
    {FK_Data_4,
     {R(DIR32LSB), R(GPREL32LSB), R(NONE), R(NONE), R(NONE), R(PCREL32LSB),
      R(FPTR32LSB), R(SECREL32LSB), R(SEGREL32LSB), R(LTV32LSB), R(NONE),
      R(NONE), R(DTPREL32LSB), R(LTOFF_FPTR32LSB), R(NONE), R(NONE), R(NONE)}},
    {FK_Data_8,
     {R(DIR64LSB), R(GPREL64LSB), R(NONE), R(NONE), R(PLTOFF64LSB),
      R(PCREL64LSB), R(FPTR64LSB), R(SECREL64LSB), R(SEGREL64LSB), R(LTV64LSB),
      R(TPREL64LSB), R(DTPMOD64LSB), R(DTPREL64LSB), R(LTOFF_FPTR64LSB),
      R(NONE), R(NONE), R(NONE)}},
    {IA64::fixup_ia64_pcrel21b, {R(PCREL21B)}},
    {IA64::fixup_ia64_pcrel21m, {R(PCREL21M)}},
    {IA64::fixup_ia64_pcrel21f, {R(PCREL21F)}},
    {IA64::fixup_ia64_pcrel60b, {R(PCREL60B)}},
    {IA64::fixup_ia64_ldxmov, {R(LDXMOV)}},
};
#undef R

unsigned IA64ELFObjectWriter::getRelocType(const MCFixup &Fixup,
                                           const MCValue &Target,
                                           bool IsPCRel) const {
  unsigned Spec = Target.getSpecifier();
  // A difference from a place in the section of the data is IP-relative
  // without saying so.
  if (IsPCRel && Fixup.getKind() < FirstTargetFixupKind && Spec == IA64::S_None)
    Spec = IA64::S_PCREL;

  switch (Spec) {
  case IA64::S_FPTR:
  case IA64::S_LTOFF_FPTR:
    // A function descriptor is for a function, not a location inside one.
    if (Target.getConstant())
      reportError(Fixup.getLoc(), "@fptr cannot take an offset");
    break;
  case IA64::S_TPREL:
  case IA64::S_DTPMOD:
  case IA64::S_DTPREL:
  case IA64::S_LTOFF_TPREL:
  case IA64::S_LTOFF_DTPMOD:
  case IA64::S_LTOFF_DTPREL:
    if (auto *Sym = const_cast<MCSymbol *>(Target.getAddSym()))
      static_cast<MCSymbolELF *>(Sym)->setType(ELF::STT_TLS);
    break;
  default:
    break;
  }

  for (const RelocRow &Row : RelocTable) {
    if (Row.Kind != Fixup.getKind())
      continue;
    if (unsigned Type = Row.Types[Spec])
      return Type;
    reportError(Fixup.getLoc(),
                Spec == IA64::S_None
                    ? "relocation is not supported here"
                    : "relocation specifier is not supported here");
    return ELF::R_IA64_NONE;
  }
  reportError(Fixup.getLoc(), "unsupported relocation type");
  return ELF::R_IA64_NONE;
}

bool IA64ELFObjectWriter::needsRelocateWithSymbol(const MCValue &V,
                                                  unsigned Type) const {
  // The linker creates one function descriptor per function, and TLS is laid
  // out per symbol.
  switch (V.getSpecifier()) {
  case IA64::S_LTOFF_FPTR:
    // GNU as keeps the symbol for instruction relocations, not data ones.
    return Type == ELF::R_IA64_LTOFF_FPTR22 ||
           Type == ELF::R_IA64_LTOFF_FPTR64I;
  case IA64::S_FPTR:
  case IA64::S_TPREL:
  case IA64::S_DTPMOD:
  case IA64::S_DTPREL:
  case IA64::S_LTOFF_TPREL:
  case IA64::S_LTOFF_DTPMOD:
  case IA64::S_LTOFF_DTPREL:
    return true;
  default:
    return false;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createIA64ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<IA64ELFObjectWriter>(OSABI);
}
