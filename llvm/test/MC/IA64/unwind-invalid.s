// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	.text
// CHECK: :[[#@LINE+1]]:2: error: .prologue outside of a procedure
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: .body outside of a procedure
	.body

	.proc a
a:
// CHECK: :[[#@LINE+1]]:8: error: expected newline
	.body 1
// CHECK: :[[#@LINE+1]]:12: error: expected a mask from 0 to 15
	.prologue 16, r32
// CHECK: :[[#@LINE+1]]:12: error: expected a mask from 0 to 15
	.prologue -1, r32
// CHECK: :[[#@LINE+1]]:12: error: expected absolute expression
	.prologue undefined, r32
// CHECK: :[[#@LINE+1]]:13: error: expected comma
	.prologue 4
// CHECK: :[[#@LINE+1]]:15: error: expected a general register
	.prologue 4, f3
// CHECK: :[[#@LINE+1]]:15: error: expected a general register
	.prologue 4, 128
// CHECK: :[[#@LINE+1]]:15: error: expected absolute expression
	.prologue 4, undefined
// CHECK: :[[#@LINE+1]]:18: error: expected newline
	.prologue 4, r32, 5
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: a prologue is already open
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: a prologue is already open
	.prologue 4, r32
	.body
// CHECK: :[[#@LINE+1]]:12: warning: a mask of 0 is the same as none
	.prologue 0, r32
	nop 0
	.endp a

// The next procedure starts afresh.
	.proc b
b:
// CHECK-NOT: error:
	.prologue
	nop 0
	.endp b
