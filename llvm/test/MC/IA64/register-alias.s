// RUN: llvm-mc -triple ia64 %s | FileCheck %s
// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -s - \
// RUN:   | FileCheck %s --check-prefix=SYM

// A symbol that is assigned a register is another name for it, wherever the
// register could be written.

// CHECK: SAVE_PFS = r34
SAVE_PFS = r34
// CHECK: RESULT = f8
RESULT = f8
	.set LINK, b0
	.equ COUNT, ar.lc
TAKEN = p7

	.proc f
f:
// CHECK: alloc r34 = ar.pfs, 2, 3, 2, 0
	alloc SAVE_PFS = ar.pfs, 2, 3, 2, 0
// CHECK: mov r8 = r34
	mov r8 = SAVE_PFS
// CHECK: ld8 r9 = [r34]
	ld8 r9 = [SAVE_PFS]
// CHECK: fma f8 = f6, f1, f8
	fadd RESULT = f6, RESULT
// CHECK: mov r3 = ar.lc
	mov r3 = COUNT
// CHECK: .save ar.pfs, r34
	.prologue
	.save ar.pfs, SAVE_PFS
	nop 0
	.body
// CHECK: (p7) br.ret.sptk.few b0
	(TAKEN) br.ret.sptk LINK
	.endp f

// It names what it was last assigned.
SAVE_PFS = r35
// CHECK: mov r8 = r35
	mov r8 = SAVE_PFS

// It may be assigned what another one names, and goes on naming that.
OTHER = SAVE_PFS
SAVE_PFS = r36
// CHECK: mov r35 = r36
	mov OTHER = SAVE_PFS

// A register of the frame is named by its number.
INPUT = in1
// CHECK: mov r8 = r33
	mov r8 = INPUT

// None of them is a symbol of the object file.
// SYM-NOT: SAVE_PFS
// SYM-NOT: RESULT
// SYM-NOT: OTHER
