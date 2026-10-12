//===-- IA64TargetObjectFile.h - IA64 Object Info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_IA64_IA64TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

class IA64TargetObjectFile : public TargetLoweringObjectFileELF {
  MCSection *ShortDataSection = nullptr;
  MCSection *ShortBSSSection = nullptr;

  /// The size of the largest object placed in short data.
  uint64_t ShortDataLimit = 8;

  /// Globals in llvm.used, which the linker must keep.
  SmallPtrSet<const GlobalValue *, 2> Retained;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Reads the SmallDataLimit module flag.
  void getModuleMetadata(Module &M) override;

  uint64_t getShortDataLimit() const { return ShortDataLimit; }

  /// Whether \p GV is short data: an object small enough to be placed with
  /// the others like it within reach of a single add to GP.
  bool isShortData(const GlobalValue *GV, const TargetMachine &TM) const;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// A function's LSDA is part of its unwind info and has no section of its
  /// own.
  MCSection *getSectionForLSDA(const Function &, const MCSymbol &,
                               const TargetMachine &) const override {
    return nullptr;
  }

  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  /// The debug info location of a TLS variable: its offset in its module's
  /// TLS block, which is what DW_OP_GNU_push_tls_address takes.
  const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64TARGETOBJECTFILE_H
