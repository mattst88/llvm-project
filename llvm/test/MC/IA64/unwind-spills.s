// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// The registers that a procedure preserves for its caller are saved by
// masks: ".save.g" for r4 to r7, ".save.f" for f2 to f5 and f16 to f31, and
// ".save.b" for b1 to b5. Each procedure here but the last has two words
// of unwind information, the header and then descriptors padded with zeroes.
// GNU as writes the same object for this file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// A general register saved in the spill area: the mask of r4 to r7, and then
// which slots of the prologue have an instruction that spills, two bits each
// from the high end of a byte, with 2 for a general register.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01d1b880 22000000
	.proc f1
f1:
	.prologue
	.save.g 0x1
	nop 0
	.body
	nop 0
	.endp f1

// A mask is of as many instructions as it has registers, and the masks of a
// prologue are told of as one.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 04d7b82a 22000000
	.proc f2
f2:
	.prologue
	nop 0
	.save.g 0x5
	nop 0
	nop 0
	.save.g 0x2
	nop 0
	.body
	nop 0
	.endp f2

// General registers saved in others, from r40 on.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03b8a8f1 0b282300
	.proc f3
f3:
	.prologue
	.save.g 0xb, r40
	nop 0
	nop 0
	nop 0
	.body
	nop 0
	.endp f3

// Floating-point registers, which are 1 in the slots: f2 to f5, and with f16
// to f31 a longer form.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01c1b840 22000000
	.proc f4
f4:
	.prologue
	.save.f 0x1
	nop 0
	.body
	nop 0
	.endp f4
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03b90800 03b85400
	.proc f5
f5:
	.prologue
	.save.f 0x3
	nop 0
	nop 0
	.save.f 0x80000
	nop 0
	.endp f5

// Branch registers, which are 3: b1 to b5, in the spill area or in general
// registers.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0283b8f0 21000000
	.proc f6
f6:
	.prologue
	.save.b 0x1
	nop 0
	.save.b 0x2
	nop 0
	.body
	nop 0
	.endp f6
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03b8fcab 28230000
	.proc f7
f7:
	.prologue
	.save.b 0x16, r40
	nop 0
	nop 0
	nop 0
	.body
	nop 0
	.endp f7

// General and floating-point registers at once. The instructions for the
// latter come first.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 06b93300 03b855a0
	.proc f8
f8:
	.prologue
	.save.gf 0x3, 0x30003
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	.endp f8
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02c1d1b8 60210000
	.proc f9
f9:
	.prologue
	.save.gf 1, 1
	nop 0
	nop 0
	.body
	nop 0
	.endp f9

// The masks come in one order whatever that of the directives is, and before
// everything else of the prologue.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03c1d181 b8e42300
	.proc f10
f10:
	.prologue
	.save.b 1
	nop 0
	.save.g 1
	nop 0
	.save.f 1
	nop 0
	.body
	nop 0
	.endp f10
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01d1b880 e0000122
	.proc f11
f11:
	.prologue
	.fframe 16
	.save.g 1
	nop 0
	.body
	nop 0
	.endp f11

// The slots are those of the region, the empty ones too.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0cc1d1b8 08001000
	.proc f12
f12:
	.prologue
	nop 0
	;;
	nop 0
	.save.g 1
	nop 0
	;;
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	.save.f 1
	nop 0
	.endp f12

// Each prologue has its own.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 02000000 00000100 01d1b880 2104c4b8
// CHECK-NEXT: 0x{{[0-9a-f]+}} 10000000 00000000
	.proc f13
f13:
	.prologue
	.save.g 1
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.save.f 4
	nop 0
	.endp f13
