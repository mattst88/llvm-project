//===-- IA64TargetStreamer.h - IA64 Target Streamer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

#include <optional>

namespace llvm {
class formatted_raw_ostream;
class MCContext;
class MCExpr;
class MCRegister;
class MCSectionELF;
class MCSymbol;

class IA64TargetStreamer : public MCTargetStreamer {
public:
  explicit IA64TargetStreamer(MCStreamer &S);
  ~IA64TargetStreamer() override;

  /// Use only the explicit stops that follow and do not infer instruction
  /// group boundaries. Without this GNU as infers them and drops the stops it
  /// is given. Assembly writes this as ".explicit".
  virtual void emitExplicitMode();

  /// End the current instruction group. Assembly writes this as ";;".
  virtual void emitStop();

  /// Use template \p Index of IA64II::BundleTemplates for the next bundle.
  /// Assembly writes this as a directive named after the template, such as
  /// ".mmi".
  virtual void emitTemplate(unsigned Index);

  /// Begin and end an explicit bundle; it is an error if the instructions do
  /// not fit in one. The preceding bundle ends without a stop, and so does
  /// this one. Assembly writes these as "{" and "}".
  virtual void emitBundleBegin();
  virtual void emitBundleEnd();

  /// Let \p Symbol be the slot of the next instruction: the address of its
  /// bundle and in the low bits which of the three it is. Assembly writes
  /// this as "[symbol:]".
  virtual void emitTag(MCSymbol *Symbol, SMLoc Loc = SMLoc());

  /// Begin a procedure with entry points \p Entries. Assembly writes this as
  /// ".proc".
  virtual void emitProc(ArrayRef<MCSymbol *> Entries);

  /// End the procedure (".endp"). Each entry symbol gets the size from it to
  /// here.
  virtual void emitEndp();

  /// Begin a prologue region at the next instruction, for the unwinder.
  /// \p Mask says which of rp (8), ar.pfs (4), psp (2) and pr (1) the
  /// prologue saves in successive general registers from \p GRSave on.
  /// Assembly writes this as ".prologue".
  virtual void emitPrologue(unsigned Mask = 0, unsigned GRSave = 0);

  /// Begin a body region at the next instruction. Assembly writes this as
  /// ".body".
  virtual void emitBody();

  /// Registers a prologue may record as saved, other than the callee-saved
  /// GRs, FRs and BRs.
  enum UnwindReg {
    UR_RP,
    UR_PFS,
    UR_PR,
    UR_UNAT,
    UR_LC,
    UR_FPSR,
    UR_RNAT,
    UR_BSP,
    UR_BSPSTORE,
    /// The NaT bits of the callee-saved GRs: ar.unat after they have been
    /// spilled.
    UR_PRIUNAT,
  };
  static const char *getUnwindRegName(UnwindReg Reg);

  /// The next instruction allocates a memory stack frame of \p Size bytes.
  /// Assembly writes this as ".fframe".
  virtual void emitFFrame(uint64_t Size);

  /// The next instruction allocates a variable-size frame, and GR \p GR
  /// holds the previous sp. Assembly writes this as ".vframe".
  virtual void emitVFrame(unsigned GR);

  /// The same, with the previous sp at \p Offset bytes from sp. Assembly
  /// writes this as ".vframesp".
  virtual void emitVFrameSP(uint64_t Offset);

  /// The callee-saved register spill area begins \p Offset bytes below the
  /// previous sp. Assembly writes this as ".spill".
  virtual void emitSpillBase(uint64_t Offset);

  /// The next instruction saves \p Reg in GR \p GR. Assembly writes this as
  /// ".save".
  virtual void emitSave(UnwindReg Reg, unsigned GR);

  /// The next instruction saves \p Reg at \p Offset bytes from sp. Assembly
  /// writes this as ".savesp".
  virtual void emitSaveSP(UnwindReg Reg, uint64_t Offset);

  /// The next instruction saves \p Reg at \p Offset bytes below the
  /// previous sp. Assembly writes this as ".savepsp".
  virtual void emitSavePSP(UnwindReg Reg, uint64_t Offset);

  /// The return address is in branch register \p BR and not in rp. Assembly
  /// writes this as ".altrp".
  virtual void emitAltRP(unsigned BR);

  /// The procedure follows the ABI and context conventions \p ABI and
  /// \p Context. Assembly writes this as ".unwabi".
  virtual void emitUnwABI(unsigned ABI, unsigned Context);

  /// The next instructions save the GRs in \p Mask (bit 0 is r4, up to r7),
  /// one per instruction and lowest first, in the spill area or in
  /// consecutive GRs starting at \p GR. Assembly writes this as ".save.g".
  virtual void emitSaveG(unsigned Mask, std::optional<unsigned> GR = {});

  /// The same for FRs f2-f5 and f16-f31, which can only go to the spill
  /// area. Assembly writes this as ".save.f".
  virtual void emitSaveF(unsigned Mask);

  /// The same for BRs b1-b5. Assembly writes this as ".save.b".
  virtual void emitSaveB(unsigned Mask, std::optional<unsigned> GR = {});

  /// The next instructions save the FRs in \p FRMask and then the GRs in
  /// \p GRMask in the spill area. Assembly writes this as ".save.gf".
  virtual void emitSaveGF(unsigned GRMask, unsigned FRMask);

  /// The next body instruction restores sp, popping the innermost prologue
  /// and \p ECount enclosing ones. Assembly writes this as ".restore sp",
  /// with the count unless \p AllOpen says it covers all open prologues,
  /// which is the meaning without a count.
  virtual void emitRestore(unsigned ECount = 0, bool AllOpen = true);

  /// Label the unwind state at this point of the body as \p Label. Assembly
  /// writes this as ".label_state".
  virtual void emitLabelState(uint64_t Label);

  /// Restore the unwind state labelled \p Label instead of the state left by
  /// the preceding code. Assembly writes this as ".copy_state".
  virtual void emitCopyState(uint64_t Label);

  /// A register operand of the per-register directives, in a prologue or a
  /// body: a class base plus a GR, FR or BR number, or one of the special
  /// registers.
  enum SpilledReg : unsigned {
    SR_GR = 0x00,
    SR_FR = 0x20,
    SR_BR = 0x40,
    SR_PR = 0x60,
    SR_PSP,
    SR_PRIUNAT,
    SR_RP,
    SR_BSP,
    SR_BSPSTORE,
    SR_RNAT,
    SR_UNAT,
    SR_FPSR,
    SR_PFS,
    SR_LC,
  };
  static void printSpilledReg(raw_ostream &OS, unsigned Reg);

  /// The next instruction saves \p Reg in the GR, FR or BR \p TReg, under
  /// predicate \p QP (p0 is always true). Assembly writes this as
  /// ".spillreg", and as ".spillreg.p" with a predicate.
  virtual void emitSpillReg(unsigned Reg, MCRegister TReg, unsigned QP = 0);

  /// The same, saving \p Reg at \p Offset bytes from sp. Assembly writes
  /// this as ".spillsp" and ".spillsp.p".
  virtual void emitSpillSP(unsigned Reg, uint64_t Offset, unsigned QP = 0);

  /// The same, saving \p Reg at \p Offset bytes below the previous sp.
  /// Assembly writes this as ".spillpsp" and ".spillpsp.p".
  virtual void emitSpillPSP(unsigned Reg, uint64_t Offset, unsigned QP = 0);

  /// The next instruction restores \p Reg from where it was saved, if
  /// \p QP is true. Assembly writes this as ".restorereg" and
  /// ".restorereg.p".
  virtual void emitRestoreReg(unsigned Reg, unsigned QP = 0);

  /// Set the procedure's personality routine to \p Routine. Assembly writes
  /// this as ".personality".
  virtual void emitPersonality(const MCSymbol *Routine);

  /// Give the procedure an unwind table entry even if it has no unwind
  /// directives. Assembly writes this as ".unwentry".
  virtual void emitUnwEntry();

  /// End the procedure's unwind information. What follows, up to the next
  /// section change, goes after it in the unwind info section as language
  /// specific data for the personality routine. Assembly writes this as
  /// ".handlerdata".
  virtual void emitHandlerData();

  /// A directive reselected the current section. GNU as ends the instruction
  /// group and the bundle at every section directive, so this does too.
  /// Assembly writes the section directive again.
  virtual void emitSectionAgain();

  /// Sixteen bytes for the dynamic linker to fill in according to \p Value,
  /// an @iplt of a function. Assembly writes this as "data16.ua".
  virtual void emitData16(const MCExpr *Value, SMLoc Loc = SMLoc());

  /// A thread-local common symbol (".tls_common"). GNU as cannot make one
  /// from ".comm" and ".type".
  virtual void emitTLSCommon(MCSymbol *Sym, uint64_t Size, Align Alignment);

  /// Space in ".bss" for a local symbol (".lcomm"). Unlike ".local" plus
  /// ".comm", GNU as does not mark the symbol as an object.
  virtual void emitLocalCommon(MCSymbol *Sym, uint64_t Size, Align Alignment);

  /// Emit the following data into \p Section, which is not the current one,
  /// until endDataElsewhere(). This is ".xdata4" and similar: unlike a
  /// section change, it does not end the current bundle.
  virtual void beginDataElsewhere(MCSection *Section);
  virtual void endDataElsewhere();

protected:
  /// The unwind table and unwind info sections for the code in \p Text.
  /// They use GNU as's names, which linker scripts may depend on.
  static std::pair<MCSectionELF *, MCSectionELF *>
  getUnwindSections(MCContext &Ctx, const MCSectionELF &Text);
};

class IA64TargetAsmStreamer : public IA64TargetStreamer {
  formatted_raw_ostream &OS;
  SmallVector<MCSymbol *, 2> Entries;

  void printEntries();
  /// Print \p Directive, with ".p" and \p QP if that is not p0, and \p Reg.
  void printSpill(StringRef Directive, unsigned Reg, unsigned QP);

public:
  IA64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitExplicitMode() override;
  void emitStop() override;
  void emitTemplate(unsigned Index) override;
  void emitBundleBegin() override;
  void emitBundleEnd() override;
  void emitTag(MCSymbol *Symbol, SMLoc Loc) override;
  void emitProc(ArrayRef<MCSymbol *> Entries) override;
  void emitEndp() override;
  void emitPrologue(unsigned Mask, unsigned GRSave) override;
  void emitBody() override;
  void emitFFrame(uint64_t Size) override;
  void emitVFrame(unsigned GR) override;
  void emitVFrameSP(uint64_t Offset) override;
  void emitSpillBase(uint64_t Offset) override;
  void emitSave(UnwindReg Reg, unsigned GR) override;
  void emitSaveSP(UnwindReg Reg, uint64_t Offset) override;
  void emitSavePSP(UnwindReg Reg, uint64_t Offset) override;
  void emitAltRP(unsigned BR) override;
  void emitUnwABI(unsigned ABI, unsigned Context) override;
  void emitSaveG(unsigned Mask, std::optional<unsigned> GR) override;
  void emitSaveF(unsigned Mask) override;
  void emitSaveB(unsigned Mask, std::optional<unsigned> GR) override;
  void emitSaveGF(unsigned GRMask, unsigned FRMask) override;
  void emitRestore(unsigned ECount, bool AllOpen) override;
  void emitLabelState(uint64_t Label) override;
  void emitCopyState(uint64_t Label) override;
  void emitSpillReg(unsigned Reg, MCRegister TReg, unsigned QP) override;
  void emitSpillSP(unsigned Reg, uint64_t Offset, unsigned QP) override;
  void emitSpillPSP(unsigned Reg, uint64_t Offset, unsigned QP) override;
  void emitRestoreReg(unsigned Reg, unsigned QP) override;
  void emitPersonality(const MCSymbol *Routine) override;
  void emitUnwEntry() override;
  void emitHandlerData() override;
  void emitSectionAgain() override;
  void emitData16(const MCExpr *Value, SMLoc Loc) override;
  void emitTLSCommon(MCSymbol *Sym, uint64_t Size, Align Alignment) override;
  void emitLocalCommon(MCSymbol *Sym, uint64_t Size, Align Alignment) override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
