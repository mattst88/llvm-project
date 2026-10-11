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

namespace llvm {
class formatted_raw_ostream;
class MCSymbol;

class IA64TargetStreamer : public MCTargetStreamer {
public:
  explicit IA64TargetStreamer(MCStreamer &S);
  ~IA64TargetStreamer() override;

  /// End the current instruction group. Assembly writes this as ";;".
  virtual void emitStop();

  /// Use template \p Index of IA64II::BundleTemplates for the next bundle.
  /// Assembly writes this as a directive named after the template, such as
  /// ".mmi".
  virtual void emitTemplate(unsigned Index);

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

  /// The next instruction of the body restores sp, and with it undoes the
  /// innermost prologue and \p ECount more around that. Assembly writes this
  /// as ".restore sp".
  virtual void emitRestore(unsigned ECount = 0);

  /// Label the unwind state at this point of the body as \p Label. Assembly
  /// writes this as ".label_state".
  virtual void emitLabelState(uint64_t Label);

  /// Restore the unwind state labelled \p Label instead of the state left by
  /// the preceding code. Assembly writes this as ".copy_state".
  virtual void emitCopyState(uint64_t Label);
};

class IA64TargetAsmStreamer : public IA64TargetStreamer {
  formatted_raw_ostream &OS;
  SmallVector<MCSymbol *, 2> Entries;

  void printEntries();

public:
  IA64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitStop() override;
  void emitTemplate(unsigned Index) override;
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
  void emitRestore(unsigned ECount) override;
  void emitLabelState(uint64_t Label) override;
  void emitCopyState(uint64_t Label) override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
