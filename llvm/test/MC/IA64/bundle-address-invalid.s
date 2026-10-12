// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=error:

// A bundle is at a multiple of 16, and data that leaves the address off one
// is not padded: GNU as gives the same error. An alignment, or the sixteen
// bytes of data16, put no bundle off.

	.text
	nop.i 0
	data4 1
// CHECK: :[[#@LINE+1]]:4: error: instruction address is not a multiple of 16
a:	nop.i 1
	.align 16
	data8 2
	data8 3
	nop.i 2
	.byte 4
	.align 8
// CHECK: :[[#@LINE+1]]:2: error: instruction address is not a multiple of 16
	nop.i 3
	.byte 5
	.align 16
	nop.i 4
	.byte 6
	.section .data
	.byte 7
	.text
// CHECK: :[[#@LINE+1]]:2: error: instruction address is not a multiple of 16
	nop.i 5
