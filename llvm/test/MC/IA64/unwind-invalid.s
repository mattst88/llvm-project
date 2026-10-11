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

	.proc d
d:
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: .restore outside of a body
	.restore sp
// CHECK: :[[#@LINE+1]]:2: error: .label_state outside of a body
	.label_state 1
// CHECK: :[[#@LINE+1]]:2: error: .copy_state outside of a body
	.copy_state 1
	nop 0
	.body
// CHECK: :[[#@LINE+1]]:11: error: expected sp
	.restore r13
// CHECK: :[[#@LINE+1]]:11: error: expected sp
	.restore 12
// CHECK: :[[#@LINE+1]]:15: error: there are 1 prologues to undo
	.restore sp, 1
// CHECK: :[[#@LINE+1]]:15: error: there are 1 prologues to undo
	.restore sp, -1
// CHECK: :[[#@LINE+1]]:15: error: expected absolute expression
	.restore sp, undefined
// CHECK: :[[#@LINE+1]]:14: error: expected newline
	.restore sp 1
// CHECK: :[[#@LINE+1]]:15: error: expected a label that is not negative
	.label_state -1
// CHECK: :[[#@LINE+1]]:14: error: no .label_state has given a state this label
	.copy_state 1
	.label_state 1
	.restore sp
	nop 0
// CHECK: :[[#@LINE+1]]:13: error: there are 0 prologues to undo
	.restore sp
// A state that is had again has its prologues again.
	.copy_state 1
// CHECK: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.restore sp
	.endp d

	.proc e
e:
	.prologue
// CHECK: :[[#@LINE+1]]:10: error: expected a mask from 1 to 0xf
	.save.g 0
// CHECK: :[[#@LINE+1]]:10: error: expected a mask from 1 to 0xf
	.save.g 16
// CHECK: :[[#@LINE+1]]:10: error: expected a mask from 1 to 0xfffff
	.save.f 0x100000
// CHECK: :[[#@LINE+1]]:10: error: expected a mask from 1 to 0x1f
	.save.b 32
// CHECK: :[[#@LINE+1]]:13: error: expected a general register
	.save.g 1, 40
// CHECK: :[[#@LINE+1]]:13: error: expected a general register
	.save.b 1, b6
// CHECK: :[[#@LINE+1]]:11: error: expected newline
	.save.f 1, r40
// CHECK: :[[#@LINE+1]]:12: error: expected comma
	.save.gf 1
// CHECK: :[[#@LINE+1]]:11: error: expected a mask from 0 to 0xf
	.save.gf 16, 0
// CHECK: :[[#@LINE+1]]:14: error: expected a mask from 0 to 0xfffff
	.save.gf 0, 0x100000
// CHECK: :[[#@LINE+1]]:11: error: expected a register in one mask at least
	.save.gf 0, 0
// A mask is of as many instructions as it has registers.
// CHECK: :[[#@LINE+1]]:2: error: fewer instructions follow than this has registers
	.save.g 3
	nop 0
	.save.g 4
	nop 0
// CHECK: :[[#@LINE+1]]:2: error: fewer instructions follow than this has registers
	.save.f 1
	.body
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: fewer instructions follow than this has registers
	.save.b 3
	nop 0
	.endp e

	.proc f
f:
	.prologue
	nop 0
	.body
// CHECK: :[[#@LINE+1]]:2: error: .save.g outside of a prologue
	.save.g 1
	nop 0
	.endp f

// The directives that tell of one register at a time.
	.proc g
g:
// CHECK: :[[#@LINE+1]]:2: error: .spillreg outside of a prologue or a body
	.spillreg r4, r8
	.prologue
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg r8, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg f6, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg f32, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg b6, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg ar.ccv, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg sp, r9
// CHECK: :[[#@LINE+1]]:12: error: expected a register that the callee preserves
	.spillreg 4, r9
// CHECK: :[[#@LINE+1]]:14: error: expected comma
	.spillreg r4
// CHECK: :[[#@LINE+1]]:16: error: expected a general, floating-point or branch register that can be written
	.spillreg r4, r0
// CHECK: :[[#@LINE+1]]:16: error: expected a general, floating-point or branch register that can be written
	.spillreg r4, f1
// CHECK: :[[#@LINE+1]]:16: error: expected a general, floating-point or branch register that can be written
	.spillreg r4, ar.lc
// CHECK: :[[#@LINE+1]]:16: error: expected a general, floating-point or branch register that can be written
	.spillreg r4, 40
// CHECK: :[[#@LINE+1]]:18: error: expected newline
	.spillreg r4, r8, r9
// CHECK: :[[#@LINE+1]]:15: error: expected a multiple of 4 that is at least 0
	.spillsp r4, -4
// CHECK: :[[#@LINE+1]]:15: error: expected a multiple of 4 that is at least 0
	.spillsp r4, 6
// CHECK: :[[#@LINE+1]]:13: error: expected comma
	.spillsp r4
// CHECK: :[[#@LINE+1]]:16: error: expected a multiple of 4 that is at least -16
	.spillpsp r4, -20
// CHECK: :[[#@LINE+1]]:16: error: expected a multiple of 4 that is at least -16
	.spillpsp r4, 2
// CHECK: :[[#@LINE+1]]:14: error: expected a register that the callee preserves
	.restorereg r8
// CHECK: :[[#@LINE+1]]:16: error: expected newline
	.restorereg r4, r8
// CHECK: :[[#@LINE+1]]:14: error: expected a predicate register
	.spillreg.p r6, r4, r8
// CHECK: :[[#@LINE+1]]:14: error: expected a predicate register
	.spillreg.p p64, r4, r8
// CHECK: :[[#@LINE+1]]:18: error: expected a register that the callee preserves
	.spillreg.p p1, r8
// CHECK: :[[#@LINE+1]]:13: error: expected a predicate register
	.spillsp.p r4, 8
// CHECK: :[[#@LINE+1]]:16: error: expected a predicate register
	.restorereg.p r4
// CHECK: :[[#@LINE+1]]:14: warning: p0 is always true, as with .spillreg
	.spillreg.p p0, r4, r8
	nop 0
	.body
// CHECK: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.restorereg r4
	.endp g

// The personality routine and the data for it.
// CHECK: :[[#@LINE+1]]:2: error: .personality outside of a procedure
	.personality routine
// CHECK: :[[#@LINE+1]]:2: error: .unwentry outside of a procedure
	.unwentry
// CHECK: :[[#@LINE+1]]:2: error: .handlerdata outside of a procedure
	.handlerdata
	.proc h
h:
// CHECK: :[[#@LINE+1]]:14: error: expected a symbol
	.personality
// CHECK: :[[#@LINE+1]]:12: error: expected newline
	.unwentry 1
	.prologue
	nop 0
// CHECK: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.save rp, r3
	.handlerdata
	.handlerdata
// CHECK: :[[#@LINE+1]]:2: error: .handlerdata after .handlerdata
	.handlerdata
// CHECK: :[[#@LINE+1]]:2: error: .body after .handlerdata
	.body
// CHECK: :[[#@LINE+1]]:2: error: .personality after .handlerdata
	.personality routine
// The code is not the section that the procedure ends in.
// CHECK: :[[#@LINE+1]]:7: error: symbol 'h' is not defined in its procedure
	.endp
