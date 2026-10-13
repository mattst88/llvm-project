// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

	.data
// CHECK: :[[#@LINE+1]]:8: error: number does not fit in the format
	real4 1e40
// Too small to be a denormal.
// CHECK: :[[#@LINE+1]]:8: error: number does not fit in the format
	real4 1e-320
// CHECK: :[[#@LINE+1]]:8: error: number does not fit in the format
	real8 1e-4940
// CHECK: :[[#@LINE+1]]:9: error: number does not fit in the format
	real16 1e5000
// A number, and not an expression.
// CHECK: :[[#@LINE+1]]:8: error: expected a floating-point number
	real4 x
// CHECK: :[[#@LINE+1]]:8: error: expected a floating-point number
	real4 (1.5)
// CHECK: :[[#@LINE+1]]:12: error: unexpected token
	real4 1.5 + 1
// GNU as takes the digits of this for ten.
// CHECK: :[[#@LINE+1]]:8: error: expected a floating-point number
	real4 0x10
// What follows "0e" is a number and nothing else.
// CHECK: :[[#@LINE+1]]:11: error: unexpected token
	real4 0e1x
// CHECK: :[[#@LINE+1]]:11: error: unexpected token
	real4 0e 1
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.real4 1.0
