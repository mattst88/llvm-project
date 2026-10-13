// RUN: llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --allow-empty --implicit-check-not=error:
// RUN: not llvm-mc -triple ia64 -filetype=obj --defsym ERR=1 %s -o /dev/null \
// RUN:   2>&1 | FileCheck %s --check-prefix=ERR --implicit-check-not=error:
// RUN: not llvm-mc -triple ia64 --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR --implicit-check-not=error:

// A directive that tells of the instruction after it waits for it through
// a label, a tag, what is said of a symbol and data that goes to another
// section. A mask of registers waits through padding as well. GNU as takes
// all of this.

	.text
	.proc f
f:
	.prologue
	nop 0
	.fframe 16
l:
	.global f
	.type f, @function
	.xdata4 .data, 1
[t:]	nop 0
	.save.g 0x1
	.align 32
	nop 0
	.endp f

.ifdef ERR
// Data, padding or another section where the instruction is to be is an
// error, as for GNU as.
	.proc g
g:
	.prologue
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.fframe 16
	.align 32
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.vframe r5
	data16 1
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.save rp, r3
	.text
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.spillreg r4, r14
	.skip 16
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.spillsp r5, 16
	.long 0, 0, 0, 0
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.spillpsp r6, 16
	.asciz "0123456789abcde"
	nop 0
	.body
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.restore sp
	.p2align 5
	nop 0
// ERR: :[[#@LINE+1]]:2: error: no instruction follows for this to tell of
	.restorereg r4
	.fill 16
	nop 0
	.endp g
.endif
