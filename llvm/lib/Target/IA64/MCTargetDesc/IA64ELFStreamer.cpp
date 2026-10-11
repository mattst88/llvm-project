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
//   - The template is the first, in encoding order, that holds the most of
//     the next three instructions. Each instruction takes the first slot of
//     the right unit after its predecessor, and skipped slots get nops.
//   - A stop after slot 0 changes the template to "M;MI", and a stop after an
//     I-unit instruction in slot 1 changes it to "MI;I", regardless of the
//     original template; the rest of the bundle is filled by the same rule. A
//     stop anywhere else ends the bundle.
//   - "alloc", "flushrs" and "loadrs" must each be first in an instruction
//     group, and a nop between the stop and the instruction would make it
//     not first. If one has to start the next bundle, the stop goes at the
//     end of the current one.
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
#include "MCTargetDesc/IA64FixupKinds.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class IA64ELFStreamer : public MCELFStreamer {
  struct PendingInst {
    MCInst Inst;
    const MCSubtargetInfo *STI;
    /// The slot units that can hold it, one bit per IA64II::Unit.
    unsigned Units;
    /// Whether it must be in the last slot of a bundle.
    bool LastSlot;
    bool Stop;
    /// The template a directive gave the bundle this instruction starts, or
    /// -1.
    int Template;
    /// The marks for this instruction's position and for the start of a
    /// region whose first instruction it is, or -1.
    int Mark;
    int StartMark;
    /// The tags, which are symbols for the slot that this one comes to be
    /// in and not for a bundle.
    SmallVector<std::pair<MCSymbol *, SMLoc>, 1> Tags;
  };

  /// A position in the instruction stream that the unwind information refers
  /// to: a bundle slot.
  struct Mark {
    MCSymbol *Bundle = nullptr;
    unsigned Slot = 0;
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

  SmallVector<Mark, 16> Marks;
  /// The marks for the upcoming instructions, next one first, or -1.
  SmallVector<int, 4> NextMarks;
  int NextStartMark = -1;
  /// The tags that the next instruction is to have.
  SmallVector<std::pair<MCSymbol *, SMLoc>, 1> NextTags;

  /// Where the last instruction ended, if only labels followed it.
  MCFragment *LastFragment = nullptr;
  size_t LastOffset = 0;
  unsigned LastEndSlot = 0;

  /// Whether output goes to a section other than the instructions' section,
  /// leaving the pending instructions untouched.
  bool Elsewhere = false;

  /// Whether the pending instructions are inside bundle braces and so form
  /// one bundle.
  bool InBundle = false;

  MCSymbol *createBundleSymbol(MCFragment &F, size_t Offset) {
    MCSymbol *Symbol = getContext().createTempSymbol();
    MCELFStreamer::emitLabelAtPos(Symbol, SMLoc(), F, Offset);
    return Symbol;
  }

  /// Write the first of \p Insts into one bundle and return how many it took.
  size_t emitBundle(MutableArrayRef<PendingInst> Insts);

public:
  IA64ELFStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> MAB,
                  std::unique_ptr<MCObjectWriter> MOW,
                  std::unique_ptr<MCCodeEmitter> MCE)
      : MCELFStreamer(Ctx, std::move(MAB), std::move(MOW), std::move(MCE)),
        MCII(createIA64MCInstrInfo()) {}

  /// Bundle the pending instructions. \p Stop also ends the instruction
  /// group. Unless \p KeepLast, a following region no longer starts right
  /// after the last instruction.
  void flush(bool Stop = true, bool KeepLast = false);

  /// Bundle the pending instructions before padding. A pending region start
  /// with only the padding before its first instruction includes the
  /// padding: it starts at the end of the last instruction's bundle.
  void flushForAlignment() {
    if (Elsewhere)
      return;
    flush(/*Stop=*/true, /*KeepLast=*/true);
    if (NextStartMark != -1 && LastFragment) {
      Marks[NextStartMark] = {createBundleSymbol(*LastFragment, LastOffset), 3};
      NextStartMark = -1;
    }
    LastFragment = nullptr;
  }

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

  /// Leave the last bundle's instruction group unchanged, whatever follows.
  void leaveGroup() { OpenFragment = nullptr; }

  /// Create a mark for the next instruction, or for the one \p Ahead of it.
  /// The mark gets a position only once the instruction is bundled.
  unsigned markNextInstruction(unsigned Ahead = 0) {
    if (NextMarks.size() <= Ahead)
      NextMarks.resize(Ahead + 1, -1);
    if (NextMarks[Ahead] == -1) {
      NextMarks[Ahead] = Marks.size();
      Marks.emplace_back();
    }
    return NextMarks[Ahead];
  }

  /// Mark the start of a region whose first instruction is the next one. The
  /// start is right after the preceding instruction, so empty slots between
  /// the two belong to the region. With no preceding instruction, or anything
  /// other than labels in between, the region starts at the bundle of the
  /// instruction.
  unsigned markRegionStart() {
    if (NextStartMark == -1) {
      NextStartMark = Marks.size();
      Marks.emplace_back();
    }
    return NextStartMark;
  }

  /// Bind the next instruction's marks to \p Symbol, since there is no next
  /// instruction.
  void bindMarks(MCSymbol *Symbol) {
    NextMarks.push_back(NextStartMark);
    for (int Mark : NextMarks)
      if (Mark != -1)
        Marks[Mark].Bundle = Symbol;
    NextMarks.clear();
    NextStartMark = -1;
  }

  const Mark &getMark(unsigned Index) const { return Marks[Index]; }

  void clearMarks() {
    assert(NextMarks.empty() && NextStartMark == -1 && Pending.empty() &&
           "a mark is still to be bound");
    Marks.clear();
  }

  void reset() override {
    Pending.clear();
    OpenFragment = nullptr;
    NextTemplate = -1;
    Marks.clear();
    NextMarks.clear();
    NextStartMark = -1;
    NextTags.clear();
    LastFragment = nullptr;
    MCELFStreamer::reset();
  }

  void emitTemplate(unsigned Index) { NextTemplate = Index; }

  void emitBundleBegin() {
    flush(/*Stop=*/false);
    InBundle = true;
  }
  void emitBundleEnd() {
    // Empty slots left by its instructions belong to the bundle, not to a
    // region that starts after it.
    bool Any = !Pending.empty();
    flush(/*Stop=*/false);
    if (Any)
      LastFragment = nullptr;
    InBundle = false;
  }

  void beginDataElsewhere(MCSection *Section) {
    assert(!Elsewhere && "already elsewhere");
    pushSection();
    Elsewhere = true;
    switchSection(Section);
  }
  void endDataElsewhere() {
    popSection();
    Elsewhere = false;
  }

  void emitTag(MCSymbol *Symbol, SMLoc Loc) {
    Symbol->redefineIfPossible();
    if (!Symbol->isUndefined()) {
      getContext().reportError(Loc, "symbol '" + Twine(Symbol->getName()) +
                                        "' is already defined");
      return;
    }
    // The tag has no address until its instruction is bundled, but it must
    // be defined from here on so that "1b" can refer to "[1:]". Until then it
    // lives in a placeholder fragment not linked into any section, so nothing
    // computes with its address.
    auto *Placeholder =
        new (getContext().allocate(sizeof(MCFragment), alignof(MCFragment)))
            MCFragment();
    Placeholder->setParent(getCurrentSectionOnly());
    Symbol->setFragment(Placeholder);
    NextTags.emplace_back(Symbol, Loc);
  }

  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;

  /// Create the line entry for a .loc that has none yet. It refers to the
  /// next instruction's slot, so unlike a label it does not end the bundle.
  void makeLineEntry() {
    MCContext &Ctx = getContext();
    if (!Ctx.getDwarfLocSeen())
      return;
    MCSymbol *LineSym = Ctx.createTempSymbol();
    NextTags.emplace_back(LineSym, SMLoc());
    MCDwarfLineEntry LineEntry(LineSym, Ctx.getCurrentDwarfLoc());
    Ctx.clearDwarfLocSeen();
    Ctx.getMCDwarfLineTable(Ctx.getDwarfCompileUnitID())
        .getMCLineSections()
        .addLineEntry(LineEntry, getCurrentSectionOnly());
  }
  void emitDwarfLocDirective(unsigned FileNo, unsigned Line, unsigned Column,
                             unsigned Flags, unsigned Isa,
                             unsigned Discriminator, StringRef FileName,
                             StringRef Comment) override {
    // Of two consecutive .locs, the first gets its entry at the same place
    // as the second.
    makeLineEntry();
    MCStreamer::emitDwarfLocDirective(FileNo, Line, Column, Flags, Isa,
                                      Discriminator, FileName, Comment);
  }

  // Anything else that has an address ends the bundle.
  void emitLabel(MCSymbol *Symbol, SMLoc Loc) override {
    // A label is not a stop: the instruction group may continue from the
    // code before it. Labels must be on bundle boundaries, though, so the
    // preceding bundle ends. A tag before the label still applies to the
    // instruction after it.
    if (InBundle && !Pending.empty())
      getContext().reportError(Loc, "label is not the first of the bundle");
    auto Tags = std::move(NextTags);
    NextTags.clear();
    flush(/*Stop=*/false);
    NextTags = std::move(Tags);
    // A label may be redefined at its current position.
    if (Symbol->isDefined() && !Symbol->isVariable() &&
        Symbol->getFragment() == getCurrentFragment() &&
        Symbol->getOffset() == getCurFragSize())
      return;
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
    flushForAlignment();
    MCELFStreamer::emitValueToAlignment(Alignment, Fill, FillLen,
                                        MaxBytesToEmit);
  }
  void emitCodeAlignment(Align Alignment, const MCSubtargetInfo &STI,
                         unsigned MaxBytesToEmit) override {
    flushForAlignment();
    MCELFStreamer::emitCodeAlignment(Alignment, STI, MaxBytesToEmit);
  }
  void emitPrefAlign(Align Alignment, const MCSymbol &End, bool EmitNops,
                     uint8_t Fill, const MCSubtargetInfo &STI) override {
    flushForAlignment();
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
  void emitData16(const MCExpr *Value, SMLoc Loc) {
    flush();
    visitUsedExpr(*Value);
    ensureHeadroom(16);
    addFixup(Value, static_cast<MCFixupKind>(IA64::fixup_ia64_iplt));
    appendContents(16, 0);
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

/// Whether slot \p Slot of template \p T can hold \p PI.
template <typename PendingInst>
static bool slotTakes(const IA64II::BundleTemplate &T, unsigned Slot,
                      const PendingInst &PI) {
  return slotTakes(T.Slots[Slot], PI.Units) && (Slot == 2 || !PI.LastSlot);
}

static uint64_t readSlot(const char *Bytes) {
  uint64_t Value = 0;
  for (unsigned I = 0; I != IA64::SlotBytes; ++I)
    Value |= uint64_t(static_cast<uint8_t>(Bytes[I])) << (8 * I);
  return Value;
}

/// Whether the instruction must immediately follow a stop.
static bool isFirstInGroup(unsigned Opcode) {
  return Opcode == IA64::ALLOC || Opcode == IA64::FLUSHRS ||
         Opcode == IA64::LOADRS;
}

/// The number of leading \p Insts that fit, by unit, in one bundle of
/// template \p T. This selects the template, as in GNU as, so it ignores
/// stops and last-slot constraints: those apply when filling the bundle and
/// may leave it with fewer instructions than counted.
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
  makeLineEntry();
  unsigned Units =
      IA64::getSlotUnits(Inst, *MCII, *getContext().getRegisterInfo());
  // See the note on the packing above.
  if (Inst.getOpcode() == IA64::HINT || Inst.getOpcode() == IA64::CHK_S)
    Units |= 1u << IA64II::UnitB;
  assert(Units != 0 && "instruction has no unit");
  bool LastSlot = (MCII->get(Inst.getOpcode()).TSFlags & IA64II::LastSlot) != 0;
  Pending.push_back({Inst, &STI, Units, LastSlot,
                     (Inst.getFlags() & IA64II::StopAfter) != 0, NextTemplate,
                     NextMarks.empty() ? -1 : NextMarks.front(), NextStartMark,
                     std::move(NextTags)});
  NextTags.clear();
  NextTemplate = -1;
  if (!NextMarks.empty())
    NextMarks.erase(NextMarks.begin());
  NextStartMark = -1;
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
    if (none_of(seq(3u), [&](unsigned Slot) {
          return slotTakes(T, Slot, Insts[0]) &&
                 IA64::getOpcodeForUnit(Insts[0].Inst.getOpcode(),
                                        T.Slots[Slot]) != 0;
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
  MCSymbol *BundleSym = nullptr;
  auto GetBundleSym = [&] {
    if (!BundleSym)
      BundleSym = createBundleSymbol(*F, BundleOffset);
    return BundleSym;
  };
  // The bundle ends at the last instruction, or where a directive starts
  // another.
  auto AtEnd = [&] {
    return Taken == Insts.size() || Insts[Taken].Template != -1;
  };
  for (unsigned Slot = 0; Slot != 3 && (Taken == 0 || !AtEnd()); ++Slot) {
    PendingInst &PI = Insts[Taken];
    IA64II::Unit Unit = IA64II::BundleTemplates[Index].Slots[Slot];
    if (!slotTakes(IA64II::BundleTemplates[Index], Slot, PI))
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
    if (PI.StartMark != -1) {
      if (LastFragment)
        Marks[PI.StartMark] = {createBundleSymbol(*LastFragment, LastOffset),
                               LastEndSlot};
      else
        Marks[PI.StartMark] = {GetBundleSym(), 0};
    }
    if (PI.Mark != -1)
      Marks[PI.Mark] = {GetBundleSym(), Slot};
    for (auto [Tag, Loc] : PI.Tags) {
      Tag->setFragment(nullptr);
      MCELFStreamer::emitLabelAtPos(Tag, Loc, *getCurrentFragment(),
                                    BundleOffset + Slot);
    }
    for (unsigned I = 0; I != Code.size(); I += IA64::SlotBytes) {
      if (I != 0)
        ++Slot;
      Slots[Slot] = readSlot(Code.data() + I);
      Used[Slot] = true;
    }
    LastFragment = F;
    LastOffset = BundleOffset;
    LastEndSlot = Slot + 1;
    F->setHasInstructions(*PI.STI);
    ++Taken;

    if (!PI.Stop)
      continue;
    if (AtEnd() || Slot == 2) {
      EndStop = true;
      break;
    }
    // The next instruction is M-unit only, so it cannot take slot 2 of MI;I.
    if (Slot == 1 && isFirstInGroup(Insts[Taken].Inst.getOpcode())) {
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

void IA64ELFStreamer::flush(bool Stop, bool KeepLast) {
  if (Elsewhere)
    return;
  // Only a label lets a region start in the preceding bundle.
  auto Break = [&] {
    if (Stop && !KeepLast)
      LastFragment = nullptr;
    // A tag with no following instruction becomes a label at the current
    // position.
    for (auto [Tag, Loc] : NextTags) {
      Tag->setFragment(nullptr);
      MCELFStreamer::emitLabel(Tag, Loc);
    }
    NextTags.clear();
  };
  if (Pending.empty()) {
    if (Stop)
      closeGroup();
    Break();
    return;
  }
  if (Stop)
    Pending.back().Stop = true;
  getCurrentSectionOnly()->ensureMinAlignment(Align(IA64II::BundleBytes));
  MutableArrayRef<PendingInst> Insts = Pending;
  while (!Insts.empty()) {
    Insts = Insts.drop_front(emitBundle(Insts));
    if (InBundle && !Insts.empty()) {
      getContext().reportError(Insts[0].Inst.getLoc(),
                               "instruction does not fit in the bundle");
      break;
    }
  }
  Pending.clear();
  Break();
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

void IA64TargetELFStreamer::emitBundleBegin() {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitBundleBegin();
}

void IA64TargetELFStreamer::emitBundleEnd() {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitBundleEnd();
}

void IA64TargetELFStreamer::emitTag(MCSymbol *Symbol, SMLoc Loc) {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitTag(Symbol, Loc);
}

void IA64TargetELFStreamer::emitProc(ArrayRef<MCSymbol *> Entries) {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  // A procedure starts on a bundle of its own, and in an instruction group
  // of its own.
  S.flush();
  // Discard unwind information from a procedure that was never ended.
  S.bindMarks(nullptr);
  Records.clear();
  S.clearMarks();
  Personality = nullptr;
  ForceEntry = InfoEmitted = false;
  ProcStart = S.getContext().createTempSymbol();
  S.emitLabel(ProcStart, SMLoc());
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

  if (InfoEmitted)
    return;
  S.bindMarks(End);
  emitUnwindInfo(End);
  Records.clear();
  S.clearMarks();
}

void IA64TargetELFStreamer::emitPersonality(const MCSymbol *Routine) {
  Personality = Routine;
}

void IA64TargetELFStreamer::emitUnwEntry() { ForceEntry = true; }

void IA64TargetELFStreamer::beginDataElsewhere(MCSection *Section) {
  static_cast<IA64ELFStreamer &>(getStreamer()).beginDataElsewhere(Section);
}

void IA64TargetELFStreamer::endDataElsewhere() {
  static_cast<IA64ELFStreamer &>(getStreamer()).endDataElsewhere();
}

void IA64TargetELFStreamer::emitData16(const MCExpr *Value, SMLoc Loc) {
  static_cast<IA64ELFStreamer &>(getStreamer()).emitData16(Value, Loc);
}

void IA64TargetELFStreamer::emitHandlerData() {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  // The unwind information covers the code up to here. Unlike the end of
  // the procedure, this does not end an instruction group, and neither does
  // the section change it makes.
  S.flush(false);
  S.leaveGroup();
  MCSymbol *End = S.getContext().createTempSymbol();
  S.emitLabel(End, SMLoc());
  S.bindMarks(End);
  ForceEntry = true;
  emitUnwindInfo(End, /*Stay=*/true);
  Records.clear();
  S.clearMarks();
  InfoEmitted = true;
}

void IA64TargetELFStreamer::addUnwindRecord(UnwindRecord::KindTy Kind,
                                            uint64_t Arg0, uint64_t Arg1,
                                            unsigned Ahead) {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  // A region header refers to the start of its region; other records refer
  // to the instruction they describe.
  bool IsRegion = Kind == UnwindRecord::Prologue || Kind == UnwindRecord::Body;
  Records.push_back(
      {Kind,
       IsRegion ? S.markRegionStart() : S.markNextInstruction(Ahead),
       {Arg0, Arg1}});
}

void IA64TargetELFStreamer::emitPrologue(unsigned Mask, unsigned GRSave) {
  addUnwindRecord(UnwindRecord::Prologue, Mask, GRSave);
}

void IA64TargetELFStreamer::emitBody() { addUnwindRecord(UnwindRecord::Body); }

bool IA64TargetELFStreamer::isInPrologueMask(unsigned Bit) const {
  for (const UnwindRecord &R : reverse(Records))
    if (R.Kind == UnwindRecord::Prologue)
      return R.Args[0] & Bit;
  return false;
}

void IA64TargetELFStreamer::emitFFrame(uint64_t Size) {
  addUnwindRecord(UnwindRecord::P7WhenSize, 0, Size / 16);
}

void IA64TargetELFStreamer::emitVFrame(unsigned GR) {
  addUnwindRecord(UnwindRecord::P7When, 1);
  if (!isInPrologueMask(2))
    addUnwindRecord(UnwindRecord::P3, 0, GR);
}

void IA64TargetELFStreamer::emitVFrameSP(uint64_t Offset) {
  addUnwindRecord(UnwindRecord::P7When, 1);
  addUnwindRecord(UnwindRecord::P7, 3, Offset / 4);
}

void IA64TargetELFStreamer::emitSpillBase(uint64_t Offset) {
  addUnwindRecord(UnwindRecord::P7, 2, (Offset + 16) / 4);
}

namespace {
/// The P3, P7 and P8 descriptor codes for a register's save time (When), save
/// GR (GR), and psp- or sp-relative save offset (PSPRel, SPRel). Registers of
/// the original format use P7 and later ones P8, marked by adding 16.
struct UnwindRegCodes {
  uint8_t When, GR, PSPRel, SPRel;
};
} // end anonymous namespace

static const UnwindRegCodes UnwindRegs[] = {
    {4, 1, 5, 1},               // UR_RP
    {6, 2, 7, 2},               // UR_PFS
    {8, 3, 9, 3},               // UR_PR
    {12, 4, 13, 5},             // UR_UNAT
    {10, 5, 11, 4},             // UR_LC
    {14, 10, 15, 6},            // UR_FPSR
    {16 + 13, 7, 16 + 14, 15},  // UR_RNAT
    {16 + 7, 8, 16 + 8, 9},     // UR_BSP
    {16 + 10, 9, 16 + 11, 12},  // UR_BSPSTORE
    {16 + 16, 11, 16 + 17, 18}, // UR_PRIUNAT
};

void IA64TargetELFStreamer::emitSave(UnwindReg Reg, unsigned GR) {
  const UnwindRegCodes &Codes = UnwindRegs[Reg];
  if (Codes.When < 16)
    addUnwindRecord(UnwindRecord::P7When, Codes.When);
  else
    addUnwindRecord(UnwindRecord::P8When, Codes.When - 16);
  // The prologue mask already gives the location of these.
  if ((Reg == UR_RP && isInPrologueMask(8)) ||
      (Reg == UR_PFS && isInPrologueMask(4)) ||
      (Reg == UR_PR && isInPrologueMask(1)))
    return;
  addUnwindRecord(UnwindRecord::P3, Codes.GR, GR);
}

void IA64TargetELFStreamer::emitSaveSP(UnwindReg Reg, uint64_t Offset) {
  const UnwindRegCodes &Codes = UnwindRegs[Reg];
  // priunat has different save time codes for a save to a register and a
  // save to memory.
  if (Reg == UR_PRIUNAT)
    addUnwindRecord(UnwindRecord::P8When, 19);
  else if (Codes.When < 16)
    addUnwindRecord(UnwindRecord::P7When, Codes.When);
  else
    addUnwindRecord(UnwindRecord::P8When, Codes.When - 16);
  addUnwindRecord(UnwindRecord::P8, Codes.SPRel, Offset / 4);
}

void IA64TargetELFStreamer::emitSavePSP(UnwindReg Reg, uint64_t Offset) {
  const UnwindRegCodes &Codes = UnwindRegs[Reg];
  if (Reg == UR_PRIUNAT)
    addUnwindRecord(UnwindRecord::P8When, 19);
  else if (Codes.When < 16)
    addUnwindRecord(UnwindRecord::P7When, Codes.When);
  else
    addUnwindRecord(UnwindRecord::P8When, Codes.When - 16);
  if (Codes.PSPRel < 16)
    addUnwindRecord(UnwindRecord::P7, Codes.PSPRel, (Offset + 16) / 4);
  else
    addUnwindRecord(UnwindRecord::P8, Codes.PSPRel - 16, (Offset + 16) / 4);
}

void IA64TargetELFStreamer::emitAltRP(unsigned BR) {
  addUnwindRecord(UnwindRecord::P3, 6, BR);
}

void IA64TargetELFStreamer::emitUnwABI(unsigned ABI, unsigned Context) {
  addUnwindRecord(UnwindRecord::P10, ABI, Context);
}

void IA64TargetELFStreamer::addSpills(unsigned Kind, unsigned Mask,
                                      unsigned &Ahead) {
  for (unsigned I = popcount(Mask); I != 0; --I)
    addUnwindRecord(UnwindRecord::Spill, Kind, 0, Ahead++);
}

void IA64TargetELFStreamer::emitSaveG(unsigned Mask,
                                      std::optional<unsigned> GR) {
  unsigned Ahead = 0;
  if (GR)
    addUnwindRecord(UnwindRecord::P9, Mask, *GR);
  else
    addUnwindRecord(UnwindRecord::GRMem, Mask);
  addSpills(2, Mask, Ahead);
}

void IA64TargetELFStreamer::emitSaveF(unsigned Mask) {
  unsigned Ahead = 0;
  addUnwindRecord(UnwindRecord::FRMem, Mask);
  addSpills(1, Mask, Ahead);
}

void IA64TargetELFStreamer::emitSaveB(unsigned Mask,
                                      std::optional<unsigned> GR) {
  unsigned Ahead = 0;
  if (GR)
    addUnwindRecord(UnwindRecord::P2, Mask, *GR);
  else
    addUnwindRecord(UnwindRecord::BRMem, Mask);
  addSpills(3, Mask, Ahead);
}

void IA64TargetELFStreamer::emitSaveGF(unsigned GRMask, unsigned FRMask) {
  unsigned Ahead = 0;
  addUnwindRecord(UnwindRecord::FRMem, FRMask);
  addUnwindRecord(UnwindRecord::GRMem, GRMask);
  addSpills(1, FRMask, Ahead);
  addSpills(2, GRMask, Ahead);
}

void IA64TargetELFStreamer::emitRestore(unsigned ECount, bool AllOpen) {
  addUnwindRecord(UnwindRecord::Epilogue, ECount);
}

void IA64TargetELFStreamer::emitLabelState(uint64_t Label) {
  addUnwindRecord(UnwindRecord::LabelState, Label);
}

void IA64TargetELFStreamer::emitCopyState(uint64_t Label) {
  addUnwindRecord(UnwindRecord::CopyState, Label);
}

void IA64TargetELFStreamer::emitSpillReg(unsigned Reg, MCRegister TReg,
                                         unsigned QP) {
  const MCRegisterInfo &MRI = *getStreamer().getContext().getRegisterInfo();
  unsigned Class = MRI.getRegClass(IA64::FRRegClassID).contains(TReg)   ? 0x80
                   : MRI.getRegClass(IA64::BRRegClassID).contains(TReg) ? 0x100
                                                                        : 0;
  addUnwindRecord(UnwindRecord::X2, QP << 8 | Reg,
                  Class | MRI.getEncodingValue(TReg));
}

void IA64TargetELFStreamer::emitSpillSP(unsigned Reg, uint64_t Offset,
                                        unsigned QP) {
  addUnwindRecord(UnwindRecord::X1, QP << 8 | 0x80 | Reg, Offset / 4);
}

void IA64TargetELFStreamer::emitSpillPSP(unsigned Reg, uint64_t Offset,
                                         unsigned QP) {
  addUnwindRecord(UnwindRecord::X1, QP << 8 | Reg, (Offset + 16) / 4);
}

void IA64TargetELFStreamer::emitRestoreReg(unsigned Reg, unsigned QP) {
  addUnwindRecord(UnwindRecord::X2, QP << 8 | Reg, 0);
}

std::optional<uint64_t>
IA64TargetELFStreamer::getFragmentOffset(const MCFragment &Fragment,
                                         bool Remember, bool FromStart) {
  const MCAssembler &Asm =
      static_cast<MCObjectStreamer &>(getStreamer()).getAssembler();
  const MCSection &Section = *Fragment.getParent();
  // Resume from the fragment the previous procedure reached, unless the
  // procedures are out of source order.
  const MCFragment *F = KnownFragment;
  uint64_t Offset = KnownOffset;
  FromStart |= !F || F->getParent() != &Section;
  if (FromStart) {
    F = &*Section.begin();
    Offset = 0;
  }
  for (; F != &Fragment; F = F->getNext()) {
    if (!F) {
      if (FromStart)
        return std::nullopt;
      FromStart = true;
      F = &*Section.begin();
      Offset = 0;
      if (F == &Fragment)
        break;
    }
    switch (F->getKind()) {
    case MCFragment::FT_Data:
      Offset += F->getFixedSize();
      break;
    case MCFragment::FT_Align: {
      Offset += F->getFixedSize();
      uint64_t Padding = offsetToAlignment(Offset, F->getAlignment());
      if (Padding <= F->getAlignMaxBytesToEmit())
        Offset += Padding;
      break;
    }
    case MCFragment::FT_Fill: {
      const auto &Fill = static_cast<const MCFillFragment &>(*F);
      int64_t Count;
      if (!Fill.getNumValues().evaluateAsAbsolute(Count, Asm) || Count < 0)
        return std::nullopt;
      Offset += Count * Fill.getValueSize();
      break;
    }
    case MCFragment::FT_Org: {
      // The offset after .org is its target, which is known if it is a
      // constant or an offset from a symbol whose offset is itself known.
      auto Known = OrgOffsets.find(F);
      if (Known != OrgOffsets.end()) {
        Offset = Known->second;
        break;
      }
      MCValue Value;
      if (!static_cast<const MCOrgFragment &>(*F)
               .getOffset()
               .evaluateAsRelocatable(Value, nullptr) ||
          Value.getSubSym())
        return std::nullopt;
      uint64_t To = Value.getConstant();
      if (const MCSymbol *Sym = Value.getAddSym()) {
        if (!Sym->isInSection() || &Sym->getSection() != &Section ||
            !OrgsInProgress.insert(F).second)
          return std::nullopt;
        std::optional<uint64_t> Base = getFragmentOffset(
            *Sym->getFragment(), /*Remember=*/false, /*FromStart=*/true);
        OrgsInProgress.erase(F);
        if (!Base)
          return std::nullopt;
        To += *Base + Sym->getOffset();
      }
      if (To < Offset)
        return std::nullopt;
      Offset = To;
      OrgOffsets[F] = To;
      break;
    }
    default:
      return std::nullopt;
    }
  }
  if (Remember) {
    KnownFragment = &Fragment;
    KnownOffset = Offset;
  }
  return Offset;
}

void IA64TargetELFStreamer::emitUnwindInfo(MCSymbol *End, bool Stay) {
  auto &S = static_cast<IA64ELFStreamer &>(getStreamer());
  MCContext &Ctx = S.getContext();

  // A prologue with an empty save mask, alone or followed by a body, is what
  // a procedure without unwind information is assumed to be anyway. If an
  // entry is still required, it has no descriptors.
  if (Records.empty() ||
      (Records[0].Kind == UnwindRecord::Prologue && Records[0].Args[0] == 0 &&
       (Records.size() == 1 ||
        (Records.size() == 2 && Records[1].Kind == UnwindRecord::Body)))) {
    if (!Personality && !ForceEntry)
      return;
    Records.clear();
  }

  // The unwinder counts in slots, three per bundle. Slot distances must be
  // known now, since the size of the encoded descriptors depends on them.
  bool Failed = false;
  std::optional<uint64_t> StartOffset =
      getFragmentOffset(*ProcStart->getFragment(), /*Remember=*/true);
  auto SlotOf = [&](const MCSymbol *Bundle, unsigned Slot) -> uint64_t {
    std::optional<uint64_t> Offset;
    if (Bundle && StartOffset)
      Offset = getFragmentOffset(*Bundle->getFragment());
    if (!Offset) {
      Failed = true;
      return 0;
    }
    uint64_t Bytes =
        *Offset + Bundle->getOffset() - (*StartOffset + ProcStart->getOffset());
    return Bytes / IA64II::BundleBytes * 3 + Slot;
  };
  auto SlotOfRecord = [&](const UnwindRecord &R) {
    const auto &[Bundle, Slot] = S.getMark(R.Mark);
    return SlotOf(Bundle, Slot);
  };
  uint64_t EndSlot = SlotOf(End, 0);

  SmallString<64> Info;
  raw_svector_ostream OS(Info);
  uint64_t RegionStart = 0, RegionLength = 0;
  for (size_t I = 0; I != Records.size(); ++I) {
    const UnwindRecord &R = Records[I];
    switch (R.Kind) {
    case UnwindRecord::Prologue:
    case UnwindRecord::Body:
      break;
    case UnwindRecord::FRMem:
    case UnwindRecord::GRMem:
    case UnwindRecord::BRMem:
    case UnwindRecord::Spill:
      // These were encoded with the region header.
      continue;
    case UnwindRecord::P2:
      OS << char(0xa0 | R.Args[0] >> 1)
         << char((R.Args[0] & 1) << 7 | R.Args[1]);
      continue;
    case UnwindRecord::P9:
      OS << char(0xf1) << char(R.Args[0]) << char(R.Args[1]);
      continue;
    case UnwindRecord::P3:
      OS << char(0xb0 | R.Args[0] >> 1)
         << char((R.Args[0] & 1) << 7 | R.Args[1]);
      continue;
    case UnwindRecord::P7:
      OS << char(0xe0 | R.Args[0]);
      encodeULEB128(R.Args[1], OS);
      continue;
    case UnwindRecord::P7When:
    case UnwindRecord::P7WhenSize:
      OS << char(0xe0 | R.Args[0]);
      encodeULEB128(SlotOfRecord(R) - RegionStart, OS);
      if (R.Kind == UnwindRecord::P7WhenSize)
        encodeULEB128(R.Args[1], OS);
      continue;
    case UnwindRecord::P8:
      OS << char(0xf0) << char(R.Args[0]);
      encodeULEB128(R.Args[1], OS);
      continue;
    case UnwindRecord::P8When:
      OS << char(0xf0) << char(R.Args[0]);
      encodeULEB128(SlotOfRecord(R) - RegionStart, OS);
      continue;
    case UnwindRecord::P10:
      OS << char(0xff) << char(R.Args[0]) << char(R.Args[1]);
      continue;
    case UnwindRecord::Epilogue: {
      // The time is counted back from the end of the region. An instruction
      // that starts the next region is treated as the last of this one.
      uint64_t When = SlotOfRecord(R) - RegionStart;
      uint64_t Time = When < RegionLength ? RegionLength - 1 - When : 0;
      if (R.Args[0] < 32) {
        OS << char(0xc0 | R.Args[0]);
        encodeULEB128(Time, OS);
      } else {
        OS << char(0xe0);
        encodeULEB128(Time, OS);
        encodeULEB128(R.Args[0], OS);
      }
      continue;
    }
    case UnwindRecord::LabelState:
    case UnwindRecord::CopyState: {
      bool IsCopy = R.Kind == UnwindRecord::CopyState;
      if (R.Args[0] < 32) {
        OS << char(0x80 | IsCopy << 5 | R.Args[0]);
      } else {
        OS << char(0xf0 | IsCopy << 3);
        encodeULEB128(R.Args[0], OS);
      }
      continue;
    }
    case UnwindRecord::X1: {
      uint8_t Reg = R.Args[0];
      if (unsigned QP = R.Args[0] >> 8)
        OS << char(0xfb) << char((Reg & 0x80) | QP) << char(Reg & 0x7f);
      else
        OS << char(0xf9) << char(Reg);
      encodeULEB128(SlotOfRecord(R) - RegionStart, OS);
      encodeULEB128(R.Args[1], OS);
      continue;
    }
    case UnwindRecord::X2: {
      // The BR class bit of the target register is stored in the byte of
      // the spilled register.
      uint8_t Reg = (R.Args[1] & 0x100) >> 1 | (R.Args[0] & 0x7f);
      if (unsigned QP = R.Args[0] >> 8)
        OS << char(0xfc) << char(QP);
      else
        OS << char(0xfa);
      OS << char(Reg) << char(R.Args[1]);
      encodeULEB128(SlotOfRecord(R) - RegionStart, OS);
      continue;
    }
    }

    // A region extends to the next region, or the last one to the end.
    const UnwindRecord *Next = std::find_if(
        Records.begin() + I + 1, Records.end(), [](const UnwindRecord &R) {
          return R.Kind == UnwindRecord::Prologue ||
                 R.Kind == UnwindRecord::Body;
        });
    uint64_t Start = RegionStart = SlotOfRecord(R);
    uint64_t Length = RegionLength =
        (Next == Records.end() ? EndSlot : SlotOfRecord(*Next)) - Start;
    bool IsBody = R.Kind == UnwindRecord::Body;
    if (!IsBody && R.Args[0] != 0) {
      // Format R2.
      OS << char(0x40 | R.Args[0] >> 1)
         << char((R.Args[0] & 1) << 7 | R.Args[1]);
      encodeULEB128(Length, OS);
    } else if (Length < 32) {
      // Format R1.
      OS << char(IsBody << 5 | Length);
    } else {
      // Format R3.
      OS << char(0x60 | IsBody);
      encodeULEB128(Length, OS);
    }

    // Prologue spill descriptors come first: the masks of registers saved to
    // the spill area, combined over all directives of the region, then two
    // bits per slot saying whether its instruction spills an FR (1), a GR (2)
    // or a BR (3).
    unsigned FRMask = 0, GRMask = 0, BRMask = 0;
    SmallVector<uint8_t, 8> IMask;
    for (const UnwindRecord *S = &R + 1; S != Next; ++S) {
      switch (S->Kind) {
      case UnwindRecord::FRMem:
        FRMask |= S->Args[0];
        break;
      case UnwindRecord::GRMem:
        GRMask |= S->Args[0];
        break;
      case UnwindRecord::BRMem:
        BRMask |= S->Args[0];
        break;
      case UnwindRecord::Spill: {
        uint64_t Slot = SlotOfRecord(*S) - Start;
        if (Failed || Slot >= Length)
          break;
        IMask.resize((Length * 2 + 7) / 8);
        IMask[Slot / 4] |= S->Args[0] << (6 - Slot % 4 * 2);
        break;
      }
      default:
        break;
      }
    }
    if (FRMask > 0xf) {
      // Format P5.
      OS << char(0xb9) << char(GRMask << 4 | FRMask >> 16) << char(FRMask >> 8)
         << char(FRMask);
    } else {
      // Format P6.
      if (FRMask != 0)
        OS << char(0xc0 | FRMask);
      if (GRMask != 0)
        OS << char(0xd0 | GRMask);
    }
    // Format P1.
    if (BRMask != 0)
      OS << char(0x80 | BRMask);
    // Format P4.
    if (!IMask.empty()) {
      OS << char(0xb8);
      OS.write(reinterpret_cast<const char *>(IMask.data()), IMask.size());
    }
  }
  if (Failed) {
    Ctx.reportError(SMLoc(), "the unwind information of a procedure needs "
                             "the distances between its instructions");
    return;
  }
  Info.resize(alignTo(Info.size(), 8));

  auto [TableSection, InfoSection] = getUnwindSections(
      Ctx, static_cast<const MCSectionELF &>(*S.getCurrentSectionOnly()));
  S.pushSection();
  S.switchSection(InfoSection);
  S.emitValueToAlignment(Align(8), 0, 1, 0);
  MCSymbol *InfoSym = Ctx.createTempSymbol();
  S.emitLabel(InfoSym, SMLoc());
  // The version, the EHANDLER and UHANDLER flags when there is a personality
  // routine, and the descriptor length in 8-byte words.
  S.emitIntValue(uint64_t(1) << 48 | uint64_t(Personality ? 3 : 0) << 32 |
                     Info.size() / 8,
                 8);
  S.emitBytes(Info);
  // The personality routine is reached through the linkage table (GOT).
  if (Personality)
    S.emitValue(
        MCSpecifierExpr::create(MCSymbolRefExpr::create(Personality, Ctx),
                                IA64::S_LTOFF_FPTR, Ctx),
        8);

  // The linker sorts the table by address.
  S.switchSection(TableSection);
  S.emitValueToAlignment(Align(8), 0, 1, 0);
  for (const MCSymbol *Sym : {ProcStart, End, InfoSym})
    S.emitValue(MCSpecifierExpr::create(MCSymbolRefExpr::create(Sym, Ctx),
                                        IA64::S_SEGREL, Ctx),
                8);
  S.popSection();
  // With ".handlerdata", stay in the info section, leaving the code section
  // as the one to return to.
  if (Stay)
    S.switchSection(InfoSection);
}

MCStreamer *llvm::createIA64ELFStreamer(const Triple &T, MCContext &Ctx,
                                        std::unique_ptr<MCAsmBackend> &&MAB,
                                        std::unique_ptr<MCObjectWriter> &&MOW,
                                        std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new IA64ELFStreamer(Ctx, std::move(MAB), std::move(MOW),
                             std::move(MCE));
}
