// RUN: not llvm-mc -triple ia64 %s -o /dev/null 2>&1 | FileCheck %s

// in0 means nothing until an alloc or a .regstk has described a frame, and
// after a .regstk that says there is none.
	mov r8 = in0
// CHECK: :[[#@LINE-1]]:11: error: no alloc or .regstk has described a frame
	.regstk 2, 3, 2, 0
	.regstk
	(p6) ld8 r8 = [loc0]
// CHECK: :[[#@LINE-1]]:17: error: no alloc or .regstk has described a frame

// The frame has as many registers of a kind as it was said to have.
	alloc r2 = ar.pfs, 2, 1, 0, 0
	mov r8 = in2
// CHECK: :[[#@LINE-1]]:11: error: the frame has 2 such registers
	mov r8 = loc1
// CHECK: :[[#@LINE-1]]:11: error: the frame has 1 such register
	mov r8 = out0
// CHECK: :[[#@LINE-1]]:11: error: the frame has 0 such registers
	alloc loc1 = ar.pfs, 2, 1, 0, 0
// CHECK: :[[#@LINE-1]]:8: error: the frame has 1 such register

	.regstk 2, 3
// CHECK: :[[#@LINE-1]]:14: error: expected comma
	.regstk 2, 3, 2, 0, 0
// CHECK: :[[#@LINE-1]]:20: error: expected newline
	.regstk 90, 3, 4, 0
// CHECK: :[[#@LINE-1]]:10: error: register stack frame is larger than 96 registers
	.regstk -1, 3, 4, 0
// CHECK: :[[#@LINE-1]]:10: error: expected a number of registers

// The names of .rotr share the registers that the frame rotates, and those of
// .rotf and .rotp the 96 and the 48 that there are.
	.regstk 0, 8, 0, 8
	.rotr a[2], b[7]
// CHECK: :[[#@LINE-1]]:16: error: more registers than the 8 that the frame rotates
	.rotf a[97]
// CHECK: :[[#@LINE-1]]:10: error: more registers than the 96 that rotate
	.rotp a[49]
// CHECK: :[[#@LINE-1]]:10: error: more registers than the 48 that rotate
	.rotf a[0]
// CHECK: :[[#@LINE-1]]:10: error: number of registers must be positive
	.rotf a[2], a[3]
// CHECK: :[[#@LINE-1]]:14: error: name is used twice
	.rotf r5[2]
// CHECK: :[[#@LINE-1]]:8: error: expected a name for the registers
	.rotf a
// CHECK: :[[#@LINE-1]]:9: error: expected '['
	.rotf a[2
// CHECK: :[[#@LINE-1]]:11: error: expected ']'

	.rotr a[2]
	mov r8 = a[2]
// CHECK: :[[#@LINE-1]]:13: error: index must be an integer in [0, 1]
	mov r8 = a[1
// CHECK: :[[#@LINE-1]]:14: error: expected ']'
	.rotr b[2]
	mov r8 = a[1]
// CHECK: :[[#@LINE-1]]:12: error: invalid operand for instruction
