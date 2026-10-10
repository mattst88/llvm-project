//===-- IA64BaseInfo.h - Top level definitions for IA64 MC ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone enum definitions for the IA64 target
// useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64BASEINFO_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64BASEINFO_H

#include <cstdint>

namespace llvm {
namespace IA64II {

/// The execution unit an instruction needs. Must match IA64Unit in
/// IA64InstrFormats.td.
enum Unit : unsigned {
  UnitNone = 0,
  UnitA = 1, ///< Either an I or an M unit.
  UnitI = 2,
  UnitM = 3,
  UnitF = 4,
  UnitB = 5,
  UnitX = 6, ///< The L and X slots of an MLX bundle.
};

// MCInstrDesc TSFlags.
enum : uint64_t {
  UnitMask = 0x7,
};

inline Unit getUnit(uint64_t TSFlags) {
  return static_cast<Unit>(TSFlags & UnitMask);
}

// MCInst flags.
enum : unsigned {
  /// The instruction ends its instruction group: a stop follows it.
  StopAfter = 1,
};

} // end namespace IA64II
} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64BASEINFO_H
