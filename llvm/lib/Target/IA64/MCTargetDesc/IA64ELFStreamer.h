//===-- IA64ELFStreamer.h - IA64 ELF streamer -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64ELFSTREAMER_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64ELFSTREAMER_H

#include "IA64TargetStreamer.h"
#include <memory>
#include <optional>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCFragment;
class MCObjectWriter;
class MCSubtargetInfo;
class MCSymbol;
class Triple;

class IA64TargetELFStreamer : public IA64TargetStreamer {
public:
  IA64TargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  void finish() override;
  void emitStop() override;
  void emitTemplate(unsigned Index) override;
  void emitProc(ArrayRef<MCSymbol *> Entries) override;
  void emitEndp() override;
  void emitPrologue(unsigned Mask, unsigned GRSave) override;
  void emitBody() override;

private:
  /// What an unwind directive said, and the instruction it said it before.
  struct UnwindRecord {
    enum KindTy { Prologue, Body } Kind;
    unsigned Mark;
    uint64_t Args[2];
  };

  void addUnwindRecord(UnwindRecord::KindTy Kind, uint64_t Arg0 = 0,
                       uint64_t Arg1 = 0);
  void emitUnwindInfo(MCSymbol *End);

  /// How far \p Fragment is into its section, if that can be told before
  /// the section is laid out: when everything before it has a size already,
  /// alignments included.
  /// \p Remember makes it the place that the next search starts from.
  std::optional<uint64_t> getFragmentOffset(const MCFragment &Fragment,
                                            bool Remember = false);
  /// A fragment with a computed offset, and that offset.
  const MCFragment *KnownFragment = nullptr;
  uint64_t KnownOffset = 0;

  SmallVector<MCSymbol *, 2> Entries;
  /// The start of the current procedure.
  MCSymbol *ProcStart = nullptr;
  SmallVector<UnwindRecord, 16> Records;
};

MCStreamer *createIA64ELFStreamer(const Triple &T, MCContext &Ctx,
                                  std::unique_ptr<MCAsmBackend> &&MAB,
                                  std::unique_ptr<MCObjectWriter> &&MOW,
                                  std::unique_ptr<MCCodeEmitter> &&MCE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64ELFSTREAMER_H
