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

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCObjectWriter;
class MCSubtargetInfo;
class Triple;

class IA64TargetELFStreamer : public IA64TargetStreamer {
public:
  IA64TargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  void finish() override;
  void emitStop() override;
  void emitTemplate(unsigned Index) override;
  void emitProc(ArrayRef<MCSymbol *> Entries) override;
  void emitEndp() override;

private:
  SmallVector<MCSymbol *, 2> Entries;
};

MCStreamer *createIA64ELFStreamer(const Triple &T, MCContext &Ctx,
                                  std::unique_ptr<MCAsmBackend> &&MAB,
                                  std::unique_ptr<MCObjectWriter> &&MOW,
                                  std::unique_ptr<MCCodeEmitter> &&MCE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64ELFSTREAMER_H
