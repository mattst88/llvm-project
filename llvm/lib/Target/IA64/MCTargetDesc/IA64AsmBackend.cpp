//===-- IA64AsmBackend.cpp - IA64 assembler backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64FixupKinds.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {

class IA64AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  explicit IA64AsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;
  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override;

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

// The relocation named by .reloc. The GNU assembler names each by the part
// after "R_IA64_"; the full name is accepted too, as on other targets.
// BFD_RELOC_NONE is the one generic name the GNU assembler can map, and is
// what codegen emits for a reference that keeps a section alive.
std::optional<MCFixupKind> IA64AsmBackend::getFixupKind(StringRef Name) const {
  if (Name == "BFD_RELOC_NONE")
    return static_cast<MCFixupKind>(FirstLiteralRelocationKind +
                                    ELF::R_IA64_NONE);
  std::string Whole;
  if (!Name.starts_with("R_IA64_"))
    Name = Whole = ("R_IA64_" + Name).str();
  unsigned Type = StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/IA64.def"
#undef ELF_RELOC
                      .Default(-1u);
  if (Type == -1u)
    return std::nullopt;
  return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
}

MCFixupKindInfo IA64AsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  // A fixup fills fields scattered over one or two slots. The offset and size
  // are those of the largest field in the code emitter's output, which is
  // what an encoding comment has room to show.
  static const MCFixupKindInfo Infos[IA64::NumTargetFixupKinds] = {
      // name, offset, bits, flags
      {"fixup_ia64_imm14", 13, 7, 0},     {"fixup_ia64_imm22", 27, 9, 0},
      {"fixup_ia64_imm64", 0, 41, 0},     {"fixup_ia64_pcrel21b", 13, 20, 0},
      {"fixup_ia64_pcrel21m", 20, 13, 0}, {"fixup_ia64_pcrel21f", 6, 20, 0},
      {"fixup_ia64_pcrel60b", 2, 39, 0},  {"fixup_ia64_tag13b", 6, 7, 0},
      {"fixup_ia64_tag13i", 24, 9, 0},    {"fixup_ia64_ldxmov", 0, 0, 0},
      {"fixup_ia64_iplt", 0, 128, 0},     {"fixup_ia64_bundle", 0, 0, 0},
  };
  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);
  assert(unsigned(Kind - FirstTargetFixupKind) < IA64::NumTargetFixupKinds &&
         "invalid kind");
  return Infos[Kind - FirstTargetFixupKind];
}

/// Move bits [\p From, \p From + \p Width) of \p Value to bit \p To.
static uint64_t field(uint64_t Value, unsigned From, unsigned Width,
                      unsigned To) {
  return ((Value >> From) & maskTrailingOnes<uint64_t>(Width)) << To;
}

/// OR \p Bits into slot \p Slot of a bundle. The fields a fixup fills are
/// zero until then.
static void orIntoSlot(uint8_t *Bundle, unsigned Slot, uint64_t Bits) {
  unsigned Pos = 5 + IA64II::SlotBits * Slot;
  Bits <<= Pos % 8;
  for (unsigned I = 0; I != 6; ++I)
    Bundle[Pos / 8 + I] |= static_cast<uint8_t>(Bits >> (8 * I));
}

void IA64AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                const MCValue &Target, uint8_t *Data,
                                uint64_t Value, bool IsResolved) {
  MCFixupKind Kind = Fixup.getKind();
  if (Kind == IA64::fixup_ia64_bundle) {
    if ((Asm->getFragmentOffset(F) + Fixup.getOffset()) % IA64II::BundleBytes)
      getContext().reportError(Fixup.getLoc(),
                               "instruction address is not a multiple of 16");
    return;
  }
  bool IsTag =
      Kind == IA64::fixup_ia64_tag13b || Kind == IA64::fixup_ia64_tag13i;
  // There is no relocation for a tag, so a preemptible global symbol
  // resolves to its local definition.
  if (!IsResolved && IsTag) {
    const MCSymbol *Sym = Target.getAddSym();
    if (!Sym || Target.getSubSym() || !Sym->isInSection() ||
        &Sym->getSection() != F.getParent()) {
      getContext().reportError(Fixup.getLoc(),
                               "tag must be a location in the same section");
      return;
    }
    Value = Asm->getSymbolOffset(*Sym) + Target.getConstant() -
            (Asm->getFragmentOffset(F) + Fixup.getOffset());
    IsResolved = true;
  }
  if (!IsResolved) {
    // A difference from a location in this section becomes IP-relative, and
    // an instruction's IP is its bundle address, without the slot number.
    MCValue Reloc = Target;
    if (Kind >= FirstTargetFixupKind && Target.getSubSym())
      Reloc = MCValue::get(Target.getAddSym(), Target.getSubSym(),
                           Target.getConstant() -
                               (Asm->getFragmentOffset(F) + Fixup.getOffset()) %
                                   IA64II::BundleBytes,
                           Target.getSpecifier());
    Asm->getWriter().recordRelocation(F, Fixup, Reloc, Value);
    return;
  }

  // A relocation specifier needs a relocation, and there is none for an
  // absolute value.
  if (Target.getSpecifier() && !Target.getAddSym()) {
    getContext().reportError(Fixup.getLoc(),
                             "a relocation specifier needs a symbol");
    return;
  }

  // The symbol of ld8.mov is there for the linker alone.
  if (Kind == IA64::fixup_ia64_ldxmov || Kind == IA64::fixup_ia64_iplt)
    return;

  if (Kind < FirstTargetFixupKind) {
    unsigned Size = getFixupKindInfo(Kind).TargetSize / 8;
    for (unsigned I = 0; I != Size; ++I)
      Data[I] = static_cast<uint8_t>(Value >> (8 * I));
    return;
  }

  // The fixup is at the address of its bundle plus the number of its slot.
  // A target is relative to the bundle.
  unsigned Slot =
      (Asm->getFragmentOffset(F) + Fixup.getOffset()) % IA64II::BundleBytes;
  uint8_t *Bundle = Data - Slot;
  if (Fixup.isPCRel()) {
    Value += Slot;
    unsigned Width = Kind == IA64::fixup_ia64_pcrel60b ? 60 : IsTag ? 9 : 21;
    // The target may be a slot address, as a tag's is; the branch goes to
    // its bundle.
    if (Value % IA64II::BundleBytes > 2) {
      getContext().reportError(Fixup.getLoc(),
                               "target is not the start of a bundle");
      return;
    }
    // A tag is only a hint, so an out-of-range one is truncated.
    if (!IsTag && !isIntN(Width + 4, static_cast<int64_t>(Value))) {
      getContext().reportError(Fixup.getLoc(), "target is out of range");
      return;
    }
    Value = static_cast<uint64_t>(static_cast<int64_t>(Value) >> 4);
  }

  auto CheckImm = [&](unsigned Width) {
    if (isIntN(Width, static_cast<int64_t>(Value)))
      return true;
    getContext().reportError(Fixup.getLoc(),
                             "value does not fit in " + Twine(Width) + " bits");
    return false;
  };

  switch (Kind) {
  case IA64::fixup_ia64_imm14:
    if (CheckImm(14))
      orIntoSlot(Bundle, Slot,
                 field(Value, 13, 1, 36) | field(Value, 7, 6, 27) |
                     field(Value, 0, 7, 13));
    break;
  case IA64::fixup_ia64_imm22:
    if (CheckImm(22))
      orIntoSlot(Bundle, Slot,
                 field(Value, 21, 1, 36) | field(Value, 16, 5, 22) |
                     field(Value, 7, 9, 27) | field(Value, 0, 7, 13));
    break;
  case IA64::fixup_ia64_imm64:
    orIntoSlot(Bundle, Slot, field(Value, 22, 41, 0));
    orIntoSlot(Bundle, Slot + 1,
               field(Value, 63, 1, 36) | field(Value, 21, 1, 21) |
                   field(Value, 16, 5, 22) | field(Value, 7, 9, 27) |
                   field(Value, 0, 7, 13));
    break;
  case IA64::fixup_ia64_pcrel21b:
    orIntoSlot(Bundle, Slot, field(Value, 20, 1, 36) | field(Value, 0, 20, 13));
    break;
  case IA64::fixup_ia64_pcrel21m:
    orIntoSlot(Bundle, Slot,
               field(Value, 20, 1, 36) | field(Value, 7, 13, 20) |
                   field(Value, 0, 7, 6));
    break;
  case IA64::fixup_ia64_pcrel21f:
    orIntoSlot(Bundle, Slot, field(Value, 20, 1, 36) | field(Value, 0, 20, 6));
    break;
  case IA64::fixup_ia64_pcrel60b:
    orIntoSlot(Bundle, Slot, field(Value, 20, 39, 2));
    orIntoSlot(Bundle, Slot + 1,
               field(Value, 59, 1, 36) | field(Value, 0, 20, 13));
    break;
  case IA64::fixup_ia64_tag13b:
    orIntoSlot(Bundle, Slot, field(Value, 7, 2, 33) | field(Value, 0, 7, 6));
    break;
  case IA64::fixup_ia64_tag13i:
    orIntoSlot(Bundle, Slot, field(Value, 0, 9, 24));
    break;
  default:
    llvm_unreachable("unknown fixup kind");
  }
}

bool IA64AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                  const MCSubtargetInfo *STI) const {
  // Code can only start at a bundle boundary, so what precedes the first
  // whole bundle of the padding is not executable.
  OS.write_zeros(Count % IA64II::BundleBytes);
  // An MMI bundle of nop.m, nop.m and nop.i. It ends with a stop bit when the
  // instruction after the padding must start an instruction group.
  const uint64_t Nop = 0x8000000;
  bool Stop = STI && STI->hasFeature(IA64::FeatureStoppedPadding);
  const uint64_t Low = 0x08 | Stop | Nop << 5 | Nop << 46;
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
