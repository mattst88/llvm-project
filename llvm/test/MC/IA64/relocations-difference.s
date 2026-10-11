// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readobj -r - | FileCheck %s

// A symbol less a place in the section of the reference is relative to the
// IP. The IP of an instruction is the address of its bundle, whichever slot
// the instruction is in.

	.text
f:
	nop.m 0
// CHECK:      0x1 R_IA64_PCREL22 x 0x0
	addl r8 = x - f, r0
// CHECK-NEXT: 0x2 R_IA64_PCREL22 x 0x4
	addl r9 = x - f + 4, r0
	nop.m 0
	nop.m 0
	nop.m 0
// CHECK-NEXT: 0x21 R_IA64_PCREL64I x 0x20
	movl r10 = x - f

	.data
d:
	.8byte 0
// CHECK:      0x8 R_IA64_PCREL64LSB x 0x8
	.8byte x - d
// CHECK-NEXT: 0x10 R_IA64_PCREL32LSB x 0x10
	.4byte x - d
