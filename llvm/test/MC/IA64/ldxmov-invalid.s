// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	ld8.mov r1 = [r2]
// CHECK: :[[#@LINE+1]]:21: error: invalid operand for instruction
	ld8.mov r1 = [r2], r3
// Only the eight-byte load has the form, and it takes no completer.
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	ld4.mov r1 = [r2], x
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	ld8.mov.nt1 r1 = [r2], x
