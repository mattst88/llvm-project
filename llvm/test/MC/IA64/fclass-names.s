// RUN: llvm-mc -triple=ia64 %s | FileCheck %s

// The classes that fclass tests for have names, which are their bits.

// CHECK:      fclass.m p6, p7 = f8, 1
// CHECK-NEXT: fclass.m p6, p7 = f8, 2
// CHECK-NEXT: fclass.m p6, p7 = f8, 64
// CHECK-NEXT: fclass.m p6, p7 = f8, 128
// CHECK-NEXT: fclass.m p6, p7 = f8, 256
	fclass.m p6, p7 = f8, @pos
	fclass.m p6, p7 = f8, @neg
	fclass.m p6, p7 = f8, @snan
	fclass.m p6, p7 = f8, @qnan
	fclass.m p6, p7 = f8, @nat

// A zero, a number or an infinity that is given no sign is one of either
// sign, whether the class is named or is a number.

// CHECK:      fclass.m p6, p7 = f8, 7
// CHECK-NEXT: fclass.m p6, p7 = f8, 11
// CHECK-NEXT: fclass.m p6, p7 = f8, 19
// CHECK-NEXT: fclass.m p6, p7 = f8, 35
// CHECK-NEXT: fclass.m p6, p7 = f8, 7
// CHECK-NEXT: fclass.m p6, p7 = f8, 263
	fclass.m p6, p7 = f8, @zero
	fclass.m p6, p7 = f8, @unorm
	fclass.m p6, p7 = f8, @norm
	fclass.m p6, p7 = f8, @inf
	fclass.m p6, p7 = f8, 4
	fclass.m p6, p7 = f8, @zero | @nat

// CHECK:      fclass.m p6, p7 = f8, 33
// CHECK-NEXT: fclass.m p6, p7 = f8, 5
// CHECK-NEXT: fclass.m p6, p7 = f8, 62
// CHECK-NEXT: fclass.m p6, p7 = f8, 448
// CHECK-NEXT: fclass.m p6, p7 = f8, 257
// CHECK-NEXT: fclass.m p6, p7 = f8, 257
// CHECK-NEXT: fclass.m.unc p7, p6 = f8, 38
	fclass.m p6, p7 = f8, @inf | @pos
	fclass.m p6, p7 = f8, @zero | 1
	fclass.m p6, p7 = f8, @zero | @unorm | @norm | @inf | @neg
	fclass.m p6, p7 = f8, @nat | @qnan | @snan
	fclass.m p6, p7 = f8, @nat + @pos
	fclass.m p6, p7 = f8, (@nat | @pos)
	fclass.nm.unc p6, p7 = f8, @zero | @neg | @inf

// CHECK:      mov r8 = 256
	mov r8 = @nat
