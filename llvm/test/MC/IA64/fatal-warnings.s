// RUN: not llvm-mc -triple ia64 -filetype=obj --fatal-warnings %s -o /dev/null \
// RUN:   2>&1 | FileCheck %s --implicit-check-not=error:

// A warning that --fatal-warnings makes an error ends its statement as an
// error does.

	.text
// CHECK: :[[#@LINE+1]]:24: error: stride must be a multiple of 64; the low 6 bits are ignored
	lfetch.count [r4], 1, 65
// CHECK: :[[#@LINE+1]]:1: error: no bundle is open
}
	.proc a
a:
// CHECK: :[[#@LINE+1]]:12: error: a mask of 0 is the same as none
	.prologue 0, r32
	.body
// CHECK: :[[#@LINE+1]]:2: error: .save.g outside of a prologue is ignored
	.save.g 0x1
	nop.i 0
// CHECK: :[[#@LINE+1]]:14: error: p0 is always true, as with .spillreg
	.spillreg.p p0, r4, r14
	nop.i 0
	.prologue
// CHECK: :[[#@LINE+1]]:2: error: .vframepsp is taken to be .vframesp
	.vframepsp 16
	nop.i 0
// CHECK: :[[#@LINE+1]]:8: error: symbol 'b' is not an entry of the procedure
	.endp b
// CHECK: :[[#@LINE+1]]:8: error: the previous .proc has no .endp
	.proc b
