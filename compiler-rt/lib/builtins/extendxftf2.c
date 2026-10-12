//===-- lib/extendxftf2.c - long double -> quad conversion --------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Assumption: long double is a IEEE 80 bit floating point type padded to 128
// bits.

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_TF_MODE) && __LDBL_MANT_DIG__ == 64 &&                     \
    (defined(__x86_64__) || defined(__ia64__))
#define SRC_80
#define DST_QUAD
#include "fp_extend_impl.inc"

COMPILER_RT_ABI tf_float __extendxftf2(xf_float a) {
#if defined(__ia64__)
  // The hardware stores denormals with a biased exponent of 1 and the integer
  // bit clear; the generic code expects an exponent of 0. Both encode the
  // same value.
  src_rep_t aRep = srcToRep(a);
  const src_rep_t integerBit = SRC_REP_C(1) << srcSigFracBits;
  const src_rep_t expMask = SRC_REP_C(0x7fff) << (srcSigFracBits + 1);
  if ((aRep & (expMask | integerBit)) == integerBit << 1)
    a = srcFromRep(aRep & ~expMask);
#endif
  return __extendXfYf2__(a);
}

#endif
