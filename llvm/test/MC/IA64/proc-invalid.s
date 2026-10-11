// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	.text
// CHECK: :[[#@LINE+1]]:8: error: .endp without a .proc
	.endp a
// CHECK: :[[#@LINE+1]]:7: error: expected a symbol
	.proc
// CHECK: :[[#@LINE+1]]:8: error: expected a symbol
	.proc 5
// CHECK: :[[#@LINE+1]]:11: error: expected a symbol
	.proc b0,
// CHECK: :[[#@LINE+1]]:11: error: expected newline
	.proc b0 b1
defined:
// CHECK: :[[#@LINE+1]]:8: error: symbol 'defined' is defined before its procedure
	.proc defined

	.proc b1
// CHECK: :[[#@LINE+1]]:8: warning: the previous .proc has no .endp
	.proc b2
b2:
// CHECK: :[[#@LINE+1]]:8: warning: symbol 'b3' is not an entry of the procedure
	.endp b3

	.proc b4
	nop 0
// CHECK: :[[#@LINE+1]]:8: error: symbol 'b4' is not defined in its procedure
	.endp b4

	.proc b5
b5:
	.data
// CHECK: :[[#@LINE+1]]:7: error: symbol 'b5' is not defined in its procedure
	.endp
	.text
	.proc b6
b6:
// CHECK: :[[#@LINE+1]]:11: error: expected newline
	.endp b6 b7
// CHECK: :[[#@LINE+1]]:8: error: expected a symbol
	.endp 5
