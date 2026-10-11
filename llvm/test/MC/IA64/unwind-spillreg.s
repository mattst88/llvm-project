// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// What the unwind information has of a register that is saved alone, in a
// prologue or in a body: in another register, or in memory at a place of
// its own and not in the spill area. Each procedure has the header and
// then one or three words of descriptors, padded with zeroes. GNU as
// writes the same object for this file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// A general, a floating-point and a branch register, each saved in another
// of its kind and then in one of another kind: the register, the one it is
// in, and the time.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 03000000 00000100 06fa0428 00fa22a8
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01fac106 02fa8707 03fa3f04 04000000
	.proc f1
f1:
	.prologue
	.spillreg r4, r40
	nop 0
	.spillreg f2, f40
	nop 0
	.spillreg b1, b6
	nop 0
	.spillreg r7, b7
	nop 0
	.spillreg f31, r4
	nop 0
	.endp f1

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03fa45ff 01000000
	.proc f2
f2:
	.prologue
	nop 0
	.spillreg b5, f127
	nop 0
	.endp f2

// The registers that have names of their own. Two that one instruction
// saves have the same time.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 03000000 00000100 06fa6303 00fa6928
// CHECK-NEXT: 0x{{[0-9a-f]+}} 00fa6301 01fa6002 02fa6703 03000000
	.proc f3
f3:
	.prologue
	.spillreg rp, r3
	.spillreg ar.pfs, r40
	nop 0
	.spillreg b0, r1
	nop 0
	.spillreg pr, r2
	nop 0
	.spillreg ar.unat, r3
	nop 0
	.endp f3

// CHECK-NEXT: 0x{{[0-9a-f]+}} 03000000 00000100 06fa6a02 00fa6203
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01fa6808 02fa6609 03fa640a 04000000
	.proc f4
f4:
	.prologue
	.spillreg ar.lc, r2
	nop 0
	.spillreg @priunat, r3
	nop 0
	.spillreg ar.fpsr, r8
	nop 0
	.spillreg ar.rnat, r9
	nop 0
	.spillreg ar.bsp, r10
	nop 0
	.endp f4

// CHECK-NEXT: 0x{{[0-9a-f]+}} 03000000 00000100 06fa6502 00fa6103
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01fa040c 02fa057f 03000000 00000000
	.proc f5
f5:
	.prologue
	.spillreg ar.bspstore, r2
	nop 0
	.spillreg psp, r3
	nop 0
	.spillreg r4, sp
	nop 0
	.spillreg r5, r127
	nop 0
	.endp f5

// In memory, at an offset from sp or below the previous sp, in words.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f98500 08000000
	.proc f6
f6:
	.prologue
	.spillsp r5, 32
	nop 0
	.endp f6

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f9b001 80200000
	.proc f7
f7:
	.prologue
	nop 0
	.spillsp f16, 0x4000
	nop 0
	.endp f7

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f93000 10000000
	.proc f8
f8:
	.prologue
	.spillpsp f16, 48
	nop 0
	.endp f8

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f96a02 00000000
	.proc f9
f9:
	.prologue
	nop 0
	nop 0
	.spillpsp ar.lc, -16
	nop 0
	.endp f9

// A body can save registers as well, and restore them.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 03000000 00000100 0125fa06 0900fa06
// CHECK-NEXT: 0x{{[0-9a-f]+}} 0001fa63 0001fc09 04000200 00000000
	.proc f10
f10:
	.prologue
	nop 0
	.body
	.spillreg r6, r9
	nop 0
	.restorereg r6
	.restorereg rp
	nop 0
	.restorereg.p p9, r4
	nop 0
	.endp f10

// Under a predicate.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03fc0604 28000000
	.proc f11
f11:
	.prologue
	.spillreg.p p6, r4, r40
	nop 0
	.endp f11

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03fc3fa2 03010000
	.proc f12
f12:
	.prologue
	nop 0
	.spillreg.p p63, f2, b3
	nop 0
	.endp f12

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03fb8705 00080000
	.proc f13
f13:
	.prologue
	.spillsp.p p7, r5, 32
	nop 0
	.endp f13

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03fb0822 00080000
	.proc f14
f14:
	.prologue
	.spillpsp.p p8, f2, 16
	nop 0
	.endp f14

// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0122fc03 60000100
	.proc f15
f15:
	.prologue
	nop 0
	.body
	nop 0
	.restorereg.p p3, pr
	nop 0
	.endp f15
