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
  /// Only the last slot of a bundle can hold the instruction.
  LastSlot = 0x8,
};

inline Unit getUnit(uint64_t TSFlags) {
  return static_cast<Unit>(TSFlags & UnitMask);
}

// MCInst flags.
enum : unsigned {
  /// The instruction ends its instruction group: a stop follows it.
  StopAfter = 1,
};

/// The most registers a register stack frame can have.
const unsigned MaxFrameSize = 96;

/// Set in a tail call's count of argument registers when the callee can
/// have the frame as it is, without an alloc to make it one of just those.
const unsigned TailKeepsFrame = 0x100;

/// Whether a branch is predicted taken, statically or dynamically.
enum BranchWhether : unsigned {
  WhetherSPTK,
  WhetherSPNT,
  WhetherDPTK,
  WhetherDPNT,
};

/// The completer for a branch prediction hint.
inline const char *getBranchWhetherName(uint64_t Whether) {
  switch (Whether) {
  case WhetherSPTK:
    return ".sptk";
  case WhetherSPNT:
    return ".spnt";
  case WhetherDPTK:
    return ".dptk";
  default:
    return ".dpnt";
  }
}

/// The completer for the locality hint of a memory access.
inline const char *getHintName(uint64_t Hint) {
  switch (Hint) {
  case 0:
    return "";
  case 1:
    return ".nt1";
  case 2:
    return ".nt2";
  case 3:
    return ".nta";
  case 4:
    return ".d4";
  case 5:
    return ".d5";
  case 6:
    return ".d6";
  case 7:
    return ".d7";
  }
  return nullptr;
}

/// The byte permutations of mux1, by their encoding. Returns null for a
/// reserved value.
inline const char *getMBTypeName(uint64_t Value) {
  switch (Value) {
  case 0x0:
    return "brcst";
  case 0x8:
    return "mix";
  case 0x9:
    return "shuf";
  case 0xa:
    return "alt";
  case 0xb:
    return "rev";
  default:
    return nullptr;
  }
}

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
