// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

// The instruction after a template directive has to have a slot in it.
	.mii
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit the MII template
	movl r4 = 5
1:
	.mmi
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit the MMI template
	fadd f6 = f7, f8
1:
	.bbb
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit the BBB template
	chk.s r4, 1b
1:
// "hint" has no B-unit form to take the slot it gets here.
	.mbb
	add r4 = r5, r6
// CHECK: :[[#@LINE+1]]:2: error: 'hint' cannot be in a B slot
	hint 1
1:
// CHECK: :[[#@LINE+1]]:7: error: unexpected token at start of statement
	.mmi 1
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.mim
