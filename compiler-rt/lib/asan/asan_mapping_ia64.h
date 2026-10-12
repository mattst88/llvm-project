//===-- asan_mapping_ia64.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// IA-64-specific definitions for ASan memory mapping.
//===----------------------------------------------------------------------===//
#ifndef ASAN_MAPPING_IA64_H
#define ASAN_MAPPING_IA64_H

// The top three bits of an address select one of eight regions. Linux uses
// four of them per process: shared libraries and anonymous mappings in region
// 1, text in region 2, data and heap in region 3, and stacks in region 4. The
// address space between regions is unimplemented, so a linear scale-and-offset
// shadow mapping cannot work.
//
// Instead, the shadow of the low 2^kRegionWindowBits bytes of each region is
// laid out consecutively in the otherwise unused region 0, indexed by region
// number:
//
//   shadow = (region << (kRegionWindowBits - scale)) | (offset >> scale)
//
// Addresses beyond the window alias within their region's shadow. The window
// is 1 TiB, the part of a region a kernel with 8K pages and three-level page
// tables maps. The default is 16K pages. A kernel with 4K pages maps only
// 64 GiB of a region unless it has four-level page tables, and has no room for
// the shadow.
//
// || `[0x2000000000000000, 0x9fffffffffffffff]` || HighMem    ||
// || `[0x0000002000000000, 0x0000009fffffffff]` || HighShadow ||
// || `[0x0000000000000000, 0x0000001fffffffff]` || ShadowGap  ||
//
// Region 0 would map onto the shadow gap, which also makes null pointer
// dereferences fault.

#define kRegionShift 61
#define kRegionWindowBits 40
#define kRegionShadowBits (kRegionWindowBits - (ASAN_SHADOW_SCALE))

#define MEM_TO_SHADOW(mem)                                    \
  ((((mem) >> kRegionShift) << kRegionShadowBits) |           \
   (((mem) & ((1ULL << kRegionWindowBits) - 1)) >> (ASAN_SHADOW_SCALE)))
#define SHADOW_TO_MEM(ptr)                         \
  ((((ptr) >> kRegionShadowBits) << kRegionShift) | \
   (((ptr) & ((1ULL << kRegionShadowBits) - 1)) << (ASAN_SHADOW_SCALE)))

// There is no low memory: all user mappings are in regions 1-4.
#define kLowMemBeg 0ULL
#define kLowMemEnd 0ULL

#define kLowShadowBeg 0ULL
#define kLowShadowEnd 0ULL

// Region 1.
#define kHighMemBeg 0x2000000000000000ULL

#define kHighShadowBeg MEM_TO_SHADOW(kHighMemBeg)
#define kHighShadowEnd MEM_TO_SHADOW(kHighMemEnd)

#define kMidShadowBeg 0ULL
#define kMidShadowEnd 0ULL

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
#define kZeroBaseShadowStart 0
#define kZeroBaseMaxShadowStart (1 << 18)

#define kShadowGapBeg kZeroBaseShadowStart
#define kShadowGapEnd (kHighShadowBeg - 1)

#define kShadowGap2Beg 0
#define kShadowGap2End 0

#define kShadowGap3Beg 0
#define kShadowGap3End 0

namespace __asan {

static inline bool AddrIsInLowMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInLowShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInMidMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInMidShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInHighMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a >= kHighMemBeg && a <= kHighMemEnd;
}

static inline bool AddrIsInHighShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a >= kHighShadowBeg && a <= kHighShadowEnd;
}

static inline bool AddrIsInShadowGap(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a <= kShadowGapEnd;
}

}  // namespace __asan

#endif  // ASAN_MAPPING_IA64_H
