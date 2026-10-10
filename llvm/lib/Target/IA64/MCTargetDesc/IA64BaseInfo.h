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
  UnitL = 7, ///< A slot kind only: the immediate half of an X-unit pair.
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

/// One of the bundle templates of SDM volume 3, table 4-2. A template fixes
/// the unit each slot goes to and where the bundle holds a stop. The low bit
/// of the 5-bit template field is a stop after slot 2; the table is indexed
/// by the other four.
struct BundleTemplate {
  const char *Name; ///< Null for a reserved template.
  Unit Slots[3];
  int MidStop; ///< The slot a stop within the bundle follows, or -1.
};

inline constexpr BundleTemplate BundleTemplates[16] = {
    {"MII", {UnitM, UnitI, UnitI}, -1},
    {"MII", {UnitM, UnitI, UnitI}, 1},
    {"MLX", {UnitM, UnitL, UnitX}, -1},
    {nullptr, {UnitNone, UnitNone, UnitNone}, -1},
    {"MMI", {UnitM, UnitM, UnitI}, -1},
    {"MMI", {UnitM, UnitM, UnitI}, 0},
    {"MFI", {UnitM, UnitF, UnitI}, -1},
    {"MMF", {UnitM, UnitM, UnitF}, -1},
    {"MIB", {UnitM, UnitI, UnitB}, -1},
    {"MBB", {UnitM, UnitB, UnitB}, -1},
    {nullptr, {UnitNone, UnitNone, UnitNone}, -1},
    {"BBB", {UnitB, UnitB, UnitB}, -1},
    {"MMB", {UnitM, UnitM, UnitB}, -1},
    {nullptr, {UnitNone, UnitNone, UnitNone}, -1},
    {"MFB", {UnitM, UnitF, UnitB}, -1},
    {nullptr, {UnitNone, UnitNone, UnitNone}, -1},
};

constexpr unsigned BundleBytes = 16;
constexpr unsigned SlotBits = 41;
constexpr uint64_t SlotMask = (uint64_t(1) << SlotBits) - 1;

} // end namespace IA64II
} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64BASEINFO_H
