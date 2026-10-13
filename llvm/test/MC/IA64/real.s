// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -s -j .data %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2
// RUN: llvm-objdump -s -j .zeroe %t | FileCheck %s --check-prefix=ZEROE

// real4 and real8 write a number in single and in double precision, aligned
// to its size. real10 writes the ten bytes of the 80-bit extended format, and
// real16 the same and six bytes of zero; both align to 16 bytes. The ".ua"
// forms do not align. GNU as writes the same bytes.

	.data
	data1 0x11
	real4 1.5, -2.25
	data1 0x22
	real8 1.5
	data1 0x33
	real10 1.5
	data1 0x44
	real16 1.5
	data1 0x55
	real4.ua 1.0
	data1 0x66
	real8.ua 3.0e10
	real10.ua 1.0
	data1 0x77
	real16.ua 1.0
	data1 0x88

// A whole number, a sign, and the names of what is not a number. GNU as has
// "nan" set every bit of the fraction, and does not take "inf" or "nan" in the
// extended format at all.
	.p2align 4
	real4 1, +2, -3, .5
	real4 inf, -inf, nan, -NaN
	real8 Inf, nan
	real10 inf
	real10 -nan

// The smallest denormal is a number; less than half of it is not.
	.p2align 4
	real4 1e-45, 3.4e38
	real8 5e-324
	REAL10 1e-4930

// CHECK:      0000 11000000 0000c03f 000010c0 22000000
// CHECK-NEXT: 0010 00000000 0000f83f 33000000 00000000
// CHECK-NEXT: 0020 00000000 000000c0 ff3f4400 00000000
// CHECK-NEXT: 0030 00000000 000000c0 ff3f0000 00000000
// CHECK-NEXT: 0040 55000080 3f660000 00b08ef0 1b420000
// CHECK-NEXT: 0050 00000000 0080ff3f 77000000 00000000
// CHECK-NEXT: 0060 80ff3f00 00000000 00880000 00000000
// CHECK-NEXT: 0070 0000803f 00000040 000040c0 0000003f
// CHECK-NEXT: 0080 0000807f 000080ff ffffff7f ffffffff
// CHECK-NEXT: 0090 00000000 0000f07f ffffffff ffffff7f
// CHECK-NEXT: 00a0 00000000 00000080 ff7f0000 00000000
// CHECK-NEXT: 00b0 ffffffff ffffffff ffff0000 00000000
// CHECK-NEXT: 00c0 01000000 9ec97f7f 01000000 00000000
// CHECK-NEXT: 00d0 9de518ec a740f2ed 0500

// GNU as has a "0e" that a number starts with say that it is a floating-point
// one, and be no part of it: "0e1" is 1. After a sign it is what it is
// anywhere else, zero times a power of ten.
	.section .zeroe,"aw",@progbits
	real4 0e0, 0e1, 0E-1, 0e+1.5e2, -0e1, 0e
	real8 0e.5
	.data
	.xreal4 .zeroe, 0e-2.5, +0e1

// ZEROE:      0000 00000000 0000803f 000080bf 00001643
// ZEROE-NEXT: 0010 00000080 00000000 00000000 0000e03f
// ZEROE-NEXT: 0020 000020c0 00000000
