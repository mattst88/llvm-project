//===-- IA64MCAsmInfo.h - IA64 Asm Info -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the IA64MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;
class MCTargetOptions;

namespace IA64 {
// Relocation specifiers, written "@name(expr)". "@ltoff" may wrap "@fptr",
// "@tprel", "@dtpmod" or "@dtprel", and each such pair is a specifier of its
// own.
enum Specifier : uint8_t {
  S_None,
  S_GPREL,
  S_LTOFF,
  S_LTOFFX,
  S_PLTOFF,
  S_PCREL,
  S_FPTR,
  S_SECREL,
  S_SEGREL,
  S_LTV,
  S_TPREL,
  S_DTPMOD,
  S_DTPREL,
  S_LTOFF_FPTR,
  S_LTOFF_TPREL,
  S_LTOFF_DTPMOD,
  S_LTOFF_DTPREL,
  S_IPLT,
};

/// The specifier that "@Name" stands for, or S_None.
Specifier parseSpecifierName(StringRef Name);
/// The specifier for \p Inner written inside "@ltoff", or S_None.
Specifier getLtoffSpecifier(Specifier Inner);
} // namespace IA64

class IA64MCAsmInfo : public MCAsmInfoELF {
public:
  explicit IA64MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
