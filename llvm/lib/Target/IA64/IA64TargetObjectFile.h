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

public:
  /// The size of the largest object placed in short data.
  static constexpr uint64_t ShortDataLimit = 8;

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Whether \p GV is short data: an object small enough to be placed with
  /// the others like it within reach of a single add to GP.
  bool isShortData(const GlobalValue *GV, const TargetMachine &TM) const;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64TARGETOBJECTFILE_H
