//===-- IA64MCTargetDesc.h - IA64 Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides IA64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

namespace llvm {
class Target;
} // end namespace llvm

// Defines symbolic names for IA64 registers.
#define GET_REGINFO_ENUM
#include "IA64GenRegisterInfo.inc"

// Defines symbolic names for the IA64 instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "IA64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "IA64GenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCTARGETDESC_H
