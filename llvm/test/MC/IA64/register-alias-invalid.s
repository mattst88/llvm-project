// RUN: not llvm-mc -triple ia64 %s -o /dev/null 2>&1 | FileCheck %s

// A name is a register from where it is assigned one.
	mov EARLY = r6
// CHECK: :[[#@LINE-1]]:6: error: invalid operand for instruction
EARLY = r5

// The names are told apart by case, as symbols are.
Lower = r5
	mov LOWER = r6
// CHECK: :[[#@LINE-1]]:6: error: invalid operand for instruction

// It is the register and not a number.
	addl r4 = Lower, r0
// CHECK: :[[#@LINE-1]]:12: error: invalid operand for instruction
	data8 Lower
// CHECK: :[[#@LINE-1]]:8: error: a register is not a value
	data8 r9
// CHECK: :[[#@LINE-1]]:8: error: a register is not a value

// A register of the frame has to have a frame.
	.regstk
NONE = in0
// CHECK: :[[#@LINE-1]]:8: error: no alloc or .regstk has described a frame
