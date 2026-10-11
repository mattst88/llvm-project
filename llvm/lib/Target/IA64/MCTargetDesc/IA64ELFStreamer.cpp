//===-- IA64ELFStreamer.cpp - IA64 ELF streamer ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ELF streamer packs instructions into bundles. A bundle is 16 bytes: a
// 5-bit template and three 41-bit slots. The template encodes the unit of
// each slot and the position of stops, so an instruction cannot be emitted
// until its neighbors are known. The streamer therefore buffers instructions
// until something needs the address after them (a label, data, an alignment,
// the end of the section) and bundles them then.
//
// Only bundles are addressable. A label between two instructions forces the
// second into a new bundle.
//
// The packing matches GNU as, so that both assemblers produce the same bytes
// for the same source:
//
//   - The template is the first one, in the order of their encodings, that
//     holds the most of the next three instructions. Each instruction takes
//     the first slot after its predecessor that has the right unit, and the
//     slots passed over hold nops.
//   - A stop after slot 0 turns the template into "M;MI" and one after an I
//     unit in slot 1 turns it into "MI;I", whatever it was, and the rest of
//     the bundle is filled by the same rule. A stop anywhere else ends the
//     bundle.
//   - "hint" and "chk.s" have no B-unit form, but the template is chosen as
//     if they had. If that assigns one a B slot, the bundle ends before it,
//     and a "hint" becomes hint.i in the next bundle.
//
// A template directive such as ".mmi" starts a new bundle with that template
// for the next instruction. Instructions fill its slots as for any other
// template, and those that do not fit go to the following bundle. The units
// are kept regardless of stops: a stop at a position where the template has
// no stop variant moves to the next position where it has one, or to the end
// of the bundle, with nops in the slots up to there. Instructions before the
// directive are packed as if it were absent, except that they end there.
//
//===----------------------------------------------------------------------===//

#include "IA64ELFStreamer.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/Support/Endian.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class IA64ELFStreamer : public MCELFStreamer {
  struct PendingInst {
    MCInst Inst;
    const MCSubtargetInfo *STI;
    /// The slot units that can hold it, one bit per IA64II::Unit.
    unsigned Units;
    bool Stop;
    /// The template a directive gave the bundle this instruction starts, or
    /// -1.
    int Template;
  };

  std::unique_ptr<MCInstrInfo> MCII;
  /// Instructions not yet bundled, all for the current section.
  SmallVector<PendingInst, 16> Pending;

  /// The last bundle emitted, while its instruction group is still open: a
  /// label flushed it, and what follows the label may still end the group.
  MCFragment *OpenFragment = nullptr;
  size_t OpenOffset = 0;

  /// The template named by a directive for the next instruction's bundle.
  int NextTemplate = -1;

  /// Write the first of \p Insts into one bundle and return how many it took.
  size_t emitBundle(MutableArrayRef<PendingInst> Insts);

public:
  IA64ELFStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> MAB,
                  std::unique_ptr<MCObjectWriter> MOW,
                  std::unique_ptr<MCCodeEmitter> MCE)
      : MCELFStreamer(Ctx, std::move(MAB), std::move(MOW), std::move(MCE)),
        MCII(createIA64MCInstrInfo()) {}

  /// Bundle the pending instructions. \p Stop ends the instruction group
  /// there too.
  void flush(bool Stop = true);

  /// End the instruction group at the last instruction.
  void emitStop() {
    if (!Pending.empty())
      Pending.back().Stop = true;
    else
      closeGroup();
  }

  /// End the instruction group of an already emitted bundle by setting its
  /// trailing stop, bit 0 of every template.
  void closeGroup() {
    if (OpenFragment)
      OpenFragment->getContents()[OpenOffset] |= 1;
    OpenFragment = nullptr;
  }

  void reset() override {
    Pending.clear();
    OpenFragment = nullptr;
    NextTemplate = -1;
    MCELFStreamer::reset();
  }

  void emitTemplate(unsigned Index) { NextTemplate = Index; }

  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;

  // Anything else that has an address ends the bundle.
  void emitLabel(MCSymbol *Symbol, SMLoc Loc) override {
    // A label is not a stop: the instruction group may run on from the code
    // before it. The label is on a bundle, though, so the one before it ends.
    flush(/*Stop=*/false);
    MCELFStreamer::emitLabel(Symbol, Loc);
  }
  void initSections(const MCSubtargetInfo &STI) override {
    MCELFStreamer::initSections(STI);
    // GNU as gives ".text" at least bundle alignment, whatever it contains.
    getContext().getObjectFileInfo()->getTextSection()->ensureMinAlignment(
        Align(IA64II::BundleBytes));
  }
  void changeSection(MCSection *Section, uint32_t Subsection) override {
    flush();
    MCELFStreamer::changeSection(Section, Subsection);
  }
  void emitBytes(StringRef Data) override {
    flush();
    MCELFStreamer::emitBytes(Data);
  }
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    flush();
    MCELFStreamer::emitValueImpl(Value, Size, Loc);
  }
  void emitULEB128Value(const MCExpr *Value) override {
    flush();
    MCELFStreamer::emitULEB128Value(Value);
  }
  void emitSLEB128Value(const MCExpr *Value) override {
    flush();
    MCELFStreamer::emitSLEB128Value(Value);
  }
  void emitValueToAlignment(Align Alignment, int64_t Fill, uint8_t FillLen,
                            unsigned MaxBytesToEmit) override {
    flush();
    MCELFStreamer::emitValueToAlignment(Alignment, Fill, FillLen,
                                        MaxBytesToEmit);
  }
  void emitCodeAlignment(Align Alignment, const MCSubtargetInfo &STI,
                         unsigned MaxBytesToEmit) override {
    flush();
    MCELFStreamer::emitCodeAlignment(Alignment, STI, MaxBytesToEmit);
  }
  void emitPrefAlign(Align Alignment, const MCSymbol &End, bool EmitNops,
                     uint8_t Fill, const MCSubtargetInfo &STI) override {
    flush();
    MCELFStreamer::emitPrefAlign(Alignment, End, EmitNops, Fill, STI);
  }
  void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                         SMLoc Loc) override {
    flush();
    MCELFStreamer::emitValueToOffset(Offset, Value, Loc);
  }
  void emitRelocDirective(const MCExpr &Offset, StringRef Name,
                          const MCExpr *Expr, SMLoc Loc) override {
    flush();
    MCELFStreamer::emitRelocDirective(Offset, Name, Expr, Loc);
  }
  using MCELFStreamer::emitFill;
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc) override {
    flush();
    MCELFStreamer::emitFill(NumBytes, FillValue, Loc);
  }
  void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                SMLoc Loc) override {
    flush();
    MCELFStreamer::emitFill(NumValues, Size, Expr, Loc);
  }
  void emitNops(int64_t NumBytes, int64_t ControlledNopLength, SMLoc Loc,
                const MCSubtargetInfo &STI) override {
    flush();
    MCELFStreamer::emitNops(NumBytes, ControlledNopLength, Loc, STI);
  }
};

} // end anonymous namespace

/// Whether a template slot of unit \p Slot can hold an instruction that
/// \p Units can execute.
static bool slotTakes(IA64II::Unit Slot, unsigned Units) {
  return (Units & 1u << Slot) != 0;
}

static uint64_t readSlot(const char *Bytes) {
  uint64_t Value = 0;
  for (unsigned I = 0; I != IA64::SlotBytes; ++I)
    Value |= uint64_t(static_cast<uint8_t>(Bytes[I])) << (8 * I);
  return Value;
}

/// How many of \p Insts fit one bundle of template \p T, stops aside.
template <typename PendingInst>
static unsigned countPlaced(const IA64II::BundleTemplate &T,
                            ArrayRef<PendingInst> Insts) {
  unsigned Slot = 0, N = 0;
  for (const PendingInst &PI : Insts) {
    while (Slot != 3 && !slotTakes(T.Slots[Slot], PI.Units))
      ++Slot;
    if (Slot == 3)
      break;
    ++N;
    Slot += T.Slots[Slot] == IA64II::UnitL ? 2 : 1;
  }
  return N;
}

void IA64ELFStreamer::emitInstruction(const MCInst &Inst,
                                      const MCSubtargetInfo &STI) {
  MCStreamer::emitInstruction(Inst, STI);
  unsigned Units =
      IA64::getSlotUnits(Inst, *MCII, *getContext().getRegisterInfo());
  // See the note on the packing above.
  if (Inst.getOpcode() == IA64::HINT || Inst.getOpcode() == IA64::CHK_S)
    Units |= 1u << IA64II::UnitB;
  assert(Units != 0 && "instruction has no unit");
  Pending.push_back({Inst, &STI, Units,
                     (Inst.getFlags() & IA64II::StopAfter) != 0, NextTemplate});
  NextTemplate = -1;
}

size_t IA64ELFStreamer::emitBundle(MutableArrayRef<PendingInst> Insts) {
  constexpr unsigned MII = 0, MISemiI = 1, MMI = 4, MSemiMI = 5;
  constexpr uint64_t Nop = 0x8000000, NopB = uint64_t(2) << 37;

  // A template from a directive must at least have a slot for the
  // instruction after the directive.
  bool Explicit = Insts[0].Template != -1;
  if (Explicit) {
    const IA64II::BundleTemplate &T =
        IA64II::BundleTemplates[Insts[0].Template];
    if (none_of(T.Slots, [&](IA64II::Unit Unit) {
          return slotTakes(Unit, Insts[0].Units) &&
                 IA64::getOpcodeForUnit(Insts[0].Inst.getOpcode(), Unit) != 0;
        })) {
      getContext().reportError(Insts[0].Inst.getLoc(),
                               Twine("instruction does not fit the ") + T.Name +
                                   " template");
      Explicit = false;
    }
  }

  unsigned Index = 0;
  if (Explicit) {
    Index = Insts[0].Template;
  } else {
    unsigned Best = 0;
    for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
      const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
      if (!T.Name || T.MidStop != -1)
        continue;
      unsigned N = countPlaced<PendingInst>(T, Insts.take_front(3));
      if (N > Best) {
        Best = N;
        Index = I;
      }
    }
    assert(Best != 0 && "no template takes the instruction");
  }

  ensureHeadroom(IA64II::BundleBytes);
  MCFragment *F = getCurrentFragment();
  size_t BundleOffset = getCurFragSize();

  uint64_t Slots[3];
  bool Used[3] = {false, false, false};
  SmallVector<MCFixup, 2> Fixups;
  SmallString<12> Code;
  size_t Taken = 0;
  bool EndStop = false;
  // The bundle ends at the last instruction, or where a directive starts
  // another.
  auto AtEnd = [&] {
    return Taken == Insts.size() || Insts[Taken].Template != -1;
  };
  for (unsigned Slot = 0; Slot != 3 && (Taken == 0 || !AtEnd()); ++Slot) {
    PendingInst &PI = Insts[Taken];
    IA64II::Unit Unit = IA64II::BundleTemplates[Index].Slots[Slot];
    if (!slotTakes(Unit, PI.Units))
      continue;

    // The slot determines the real instruction for a pseudo-op such as "nop".
    MCInst Inst = PI.Inst;
    Inst.setOpcode(IA64::getOpcodeForUnit(Inst.getOpcode(), Unit));
    if (Inst.getOpcode() == 0) {
      assert(Unit == IA64II::UnitB && Taken != 0 && "no form for the slot");
      if (PI.Inst.getOpcode() == IA64::HINT) {
        if (Explicit)
          getContext().reportError(PI.Inst.getLoc(),
                                   "'hint' cannot be in a B slot");
        PI.Units = 1u << IA64II::UnitI;
      }
      break;
    }
    size_t FirstFixup = Fixups.size();
    Code.clear();
    getAssembler().getEmitter().encodeInstruction(Inst, Code, Fixups, *PI.STI);
    // A fixup encodes the slot in the low bits of its offset; for an X-unit
    // instruction that is the L slot.
    for (MCFixup &Fixup : MutableArrayRef(Fixups).drop_front(FirstFixup))
      Fixup.setOffset(BundleOffset + Slot);
    for (unsigned I = 0; I != Code.size(); I += IA64::SlotBytes) {
      if (I != 0)
        ++Slot;
      Slots[Slot] = readSlot(Code.data() + I);
      Used[Slot] = true;
    }
    F->setHasInstructions(*PI.STI);
    ++Taken;

    if (!PI.Stop)
      continue;
    if (AtEnd() || Slot == 2) {
      EndStop = true;
      break;
    }
    const IA64II::BundleTemplate &T = IA64II::BundleTemplates[Index];
    if (Explicit) {
      // Only MMI has a form with a stop after slot 0, and only MII one with
      // a stop after slot 1.
      if (Slot == 0 && Index == MMI) {
        Index = MSemiMI;
      } else if (Index == MII) {
        Index = MISemiI;
        Slot = 1;
      } else {
        EndStop = true;
        break;
      }
    } else if (Slot == 0 && T.Slots[0] == IA64II::UnitM) {
      Index = MSemiMI;
    } else if (Slot == 1 && T.Slots[1] == IA64II::UnitI && T.MidStop == -1) {
      Index = MISemiI;
    } else {
      EndStop = true;
      break;
    }
  }

  const IA64II::BundleTemplate &T = IA64II::BundleTemplates[Index];
  for (unsigned Slot = 0; Slot != 3; ++Slot)
    if (!Used[Slot])
      Slots[Slot] = T.Slots[Slot] == IA64II::UnitB   ? NopB
                    : T.Slots[Slot] == IA64II::UnitL ? 0
                                                     : Nop;

  char Bytes[IA64II::BundleBytes];
  support::endian::write64le(Bytes, (Index << 1 | EndStop) | Slots[0] << 5 |
                                        Slots[1] << 46);
  support::endian::write64le(Bytes + 8, Slots[1] >> 18 | Slots[2] << 23);
  appendContents(ArrayRef(Bytes));
  OpenFragment = EndStop ? nullptr : F;
  OpenOffset = BundleOffset;
  // Headroom was reserved, so the bundle is at the offset the fixups use.
  assert(getCurrentFragment() == F && "bundle went to another fragment");
  F->appendFixups(Fixups);
  return Taken;
}

void IA64ELFStreamer::flush(bool Stop) {
  if (Pending.empty()) {
    if (Stop)
      closeGroup();
    return;
  }
  if (Stop)
    Pending.back().Stop = true;
  getCurrentSectionOnly()->ensureMinAlignment(Align(IA64II::BundleBytes));
  MutableArrayRef<PendingInst> Insts = Pending;
  while (!Insts.empty())
    Insts = Insts.drop_front(emitBundle(Insts));
  Pending.clear();
}

IA64TargetELFStreamer::IA64TargetELFStreamer(MCStreamer &S,
                                             const MCSubtargetInfo &STI)
    : IA64TargetStreamer(S) {
  static_cast<MCELFStreamer &>(S).getWriter().setELFHeaderEFlags(
      ELF::EF_IA_64_ABI64);
}

void IA64TargetELFStreamer::finish() {
  static_cast<IA64ELFStreamer &>(getStreamer()).flush();
}

void IA64TargetELFStreamer::emitStop() {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitStop();
}

void IA64TargetELFStreamer::emitTemplate(unsigned Index) {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitTemplate(Index);
}

void IA64TargetELFStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  // A procedure starts on a bundle of its own, and in an instruction group
  // of its own.
  S.flush();
  this->Entries.assign(Entries.begin(), Entries.end());
  for (MCSymbol *Entry : Entries)
    S.emitSymbolAttribute(Entry, MCSA_ELF_TypeFunction);
}

void IA64TargetELFStreamer::emitEndp() {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  MCContext &Ctx = S.getContext();
  S.flush();
  MCSymbol *End = Ctx.createTempSymbol();
  S.emitLabel(End, SMLoc());
  // An explicit size from the source takes precedence.
  for (MCSymbol *Entry : Entries) {
    auto *Sym = static_cast<MCSymbolELF *>(Entry);
    if (!Sym->getSize())
      Sym->setSize(MCBinaryExpr::createSub(MCSymbolRefExpr::create(End, Ctx),
                                           MCSymbolRefExpr::create(Sym, Ctx),
                                           Ctx));
  }
  Entries.clear();
}

MCStreamer *llvm::createIA64ELFStreamer(const Triple &T, MCContext &Ctx,
                                        std::unique_ptr<MCAsmBackend> &&MAB,
                                        std::unique_ptr<MCObjectWriter> &&MOW,
                                        std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new IA64ELFStreamer(Ctx, std::move(MAB), std::move(MOW),
                             std::move(MCE));
}
