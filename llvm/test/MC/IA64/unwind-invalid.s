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

	.proc c
c:
// CHECK: :[[#@LINE+1]]:2: error: .fframe outside of a prologue
	.fframe 16
	.body
// CHECK: :[[#@LINE+1]]:2: error: .save outside of a prologue
	.save rp, r32
	.prologue 13, r32
// CHECK: :[[#@LINE+1]]:10: error: expected a multiple of 16 that is at least 0
	.fframe 24
// CHECK: :[[#@LINE+1]]:10: error: expected a multiple of 16 that is at least 0
	.fframe -16
// CHECK: :[[#@LINE+1]]:10: error: expected absolute expression
	.fframe undefined
// CHECK: :[[#@LINE+1]]:10: error: expected a general register
	.vframe 34
// CHECK: :[[#@LINE+1]]:10: error: expected a general register
	.vframe b0
// CHECK: :[[#@LINE+1]]:12: error: expected a multiple of 4 that is at least 0
	.vframesp 6
// CHECK: :[[#@LINE+1]]:9: error: expected a multiple of 4 that is at least -16
	.spill -20
// CHECK: :[[#@LINE+1]]:8: error: expected a register that a prologue saves
	.save r4, r32
// CHECK: :[[#@LINE+1]]:8: error: expected a register that a prologue saves
	.save ar.ccv, r32
// CHECK: :[[#@LINE+1]]:8: error: expected a register that a prologue saves
	.save @unat, r32
// CHECK: :[[#@LINE+1]]:10: error: expected comma
	.save rp
// CHECK: :[[#@LINE+1]]:12: error: expected a general register
	.save rp, 32
// CHECK: :[[#@LINE+1]]:14: error: expected a multiple of 4 that is at least 0
	.savesp rp, 2
// CHECK: :[[#@LINE+1]]:15: error: expected a multiple of 4 that is at least -16
	.savepsp rp, -32
// CHECK: :[[#@LINE+1]]:9: error: expected a branch register
	.altrp r2
// CHECK: :[[#@LINE+1]]:10: error: expected @svr4, @hpux, @nt or a number
	.unwabi @linux, 0
// CHECK: :[[#@LINE+1]]:10: error: expected a number from 0 to 255
	.unwabi 256, 0
// CHECK: :[[#@LINE+1]]:13: error: expected a number from 0 to 255
	.unwabi 0, 256
// CHECK: :[[#@LINE+1]]:11: error: expected comma
	.unwabi 0
	nop 0
// CHECK: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.save ar.lc, r2
	.endp c
