//===-- divxf3.S - Divide extended-precision values -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// long double __divxf3(long double a, long double b);
//
// IA-64 has no divide instruction. frcpa gives y0, an approximation of 1/b
// with relative error below 2^-8.886, and the error term d = 1 - b*y0 is exact
// enough to sum the series 1/b = y0 * (1 + d + d^2 + ...) to the full 64-bit
// significand: y2 = y0 * (1 + d + ... + d^6) has error d^7, and one more
// Newton-Raphson step squares that.
//
// The quotient is refined the same way. q1 is within 1 ulp of a/b, so the
// remainder a - b*q1 is exactly representable in a register, and the final
// fma adds remainder * reciprocal to q1 with the only rounding, in the
// caller's status field (sf0).
//
// All other steps use status field 1, which by software convention rounds to
// the full register significand, has the widest exponent range and has all
// traps disabled, so intermediate results are invisible to the caller.
//
// For zeroes, infinities and NaNs, frcpa clears the predicate, which skips the
// iteration, and returns the quotient itself.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

	.text
	.align 32
	.proc __divxf3
DEFINE_COMPILERRT_FUNCTION(__divxf3)
	frcpa.s0 f10, p6 = f8, f9 ;;
(p6)	fnma.s1	f11 = f9, f10, f1	// d = 1 - b * y0
(p6)	fma.s1	f12 = f8, f10, f0 ;;	// q0 = a * y0
(p6)	fma.s1	f13 = f11, f11, f0	// d^2
(p6)	fma.s1	f14 = f11, f11, f11 ;;	// d + d^2
(p6)	fma.s1	f13 = f13, f13, f11	// d + d^4
(p6)	fma.s1	f14 = f14, f10, f10 ;;	// y1 = y0 * (1 + d + d^2)
(p6)	fma.s1	f13 = f13, f14, f10	// y2 = y0 + (d + d^4) * y1
(p6)	fnma.s1	f15 = f9, f12, f8 ;;	// r0 = a - b * q0
(p6)	fma.s1	f12 = f15, f13, f12	// q1 = q0 + r0 * y2
(p6)	fnma.s1	f11 = f9, f13, f1 ;;	// e = 1 - b * y2
(p6)	fma.s1	f13 = f11, f13, f13	// y3 = y2 + e * y2
(p6)	fnma.s1	f15 = f9, f12, f8 ;;	// r = a - b * q1
(p6)	fma.s0	f10 = f15, f13, f12 ;;	// q = q1 + r * y3
	mov	f8 = f10
	br.ret.sptk.many b0
END_COMPILERRT_FUNCTION(__divxf3)
	.endp __divxf3

NO_EXEC_STACK_DIRECTIVE
