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
