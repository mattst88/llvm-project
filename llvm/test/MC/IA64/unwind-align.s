// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s

// Where a region begins when its first instruction is aligned. GNU as
// writes the same object for this file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// The padding of an alignment that comes between the directive for a
// region and the first instruction of it is in the region: the prologue
// here is the three slots of its one bundle and the body is six.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e00001 26000000
	.proc g1
g1:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	.body
	.align 32
	nop 0
	br.ret.sptk.many b0
	.endp g1
// Nothing changes when there is no padding.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e00001 23000000
	.align 32
	.proc g2
g2:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	.body
	.align 16
	nop 0
	br.ret.sptk.many b0
	.endp g2
// A label is not padding. The region begins right after the instruction
// before it, in the slots of that bundle that were left empty.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e00001 25000000
	.align 32
	.proc g3
g3:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	.body
.L1:
	nop 0
	br.ret.sptk.many b0
	.endp g3
// The same for each of three regions, with three bundles of padding in
// each.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e00001 2c0c2c00
	.align 32
	.proc g4
g4:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	nop 0
	nop 0
	.body
	.align 64
	nop 0
	.prologue
	.align 64
	nop 0
	.body
	.align 64
	br.ret.sptk.many b0
	.endp g4
// The time of a restore of sp counts back from the end of the body, so
// the padding before it does not show.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e00001 26c00000
	.align 32
	.proc g5
g5:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	nop 0
	.body
	.align 32
	nop 0
	.restore sp
	br.ret.sptk.many b0
	.endp g5
