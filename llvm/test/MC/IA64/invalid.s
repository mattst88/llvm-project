// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

// CHECK: :[[#@LINE+1]]:11: error: immediate must be an integer in [-128, 127]
	sub r1 = 128, r2
// CHECK: :[[#@LINE+1]]:12: error: immediate must be an integer in [-8192, 8191]
	adds r1 = 8192, r2
// CHECK: :[[#@LINE+1]]:12: error: immediate must be an integer in [-2097152, 2097151]
	addl r1 = -2097153, r2
// addl has a two-bit base register field.
// CHECK: :[[#@LINE+1]]:15: error: invalid operand for instruction
	addl r1 = 1, r4
// CHECK: :[[#@LINE+1]]:18: error: shift count must be an integer in [1, 4]
	shladd r1 = r2, 5, r3
// CHECK: :[[#@LINE+1]]:20: error: shift count must be an integer in [1, 3]
	pshladd2 r1 = r2, 4, r3
// CHECK: :[[#@LINE+1]]:6: error: invalid operand for instruction
	add f1 = r2, r3
// CHECK: :[[#@LINE+1]]:3: error: expected a predicate register
	(r6) add r1 = r2, r3
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	frob r1 = r2, r3
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	add r1 = r2

// I-unit operands.
// CHECK: :[[#@LINE+1]]:26: error: invalid operand for instruction
	pmpyshr2.u r4 = r5, r6, 8
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	mux2 r4 = r5, 256
// CHECK: :[[#@LINE+1]]:16: error: unknown operand '@foo'
	mux1 r4 = r5, @foo
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	pshr4.u r4 = r6, 32
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	extr r4 = r6, 5, 0
// CHECK: :[[#@LINE+1]]:13: error: immediate must be an integer in [-128, 127]
	dep.z r4 = 128, 40, 9
// CHECK: :[[#@LINE+1]]:11: error: invalid operand for instruction
	dep r4 = 1, r6, 20, 33
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	dep r4 = r5, r6, 17, 17
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	tf.z p6, p7 = 31
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.i 0x200000

// An IP-relative target is a whole number of bundles away.
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, 8
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, 0x1000000
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, -0x1000010
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-4096, 4080]
	mov b6 = r4, 4096
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-4096, 4080]
	mov b6 = r4, 24
