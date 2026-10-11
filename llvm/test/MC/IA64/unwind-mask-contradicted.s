// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t 2>&1 | FileCheck %s --check-prefix=WARN
// RUN: llvm-readobj -x .IA_64.unwind_info %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s 2>/dev/null \
// RUN:   | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// The mask of a prologue has said where rp, ar.pfs, the previous sp and pr
// are. GNU as only warns of a directive that has them elsewhere, and goes by
// the mask.

	.text
	.proc f
f:
	.prologue 15, r40
// WARN: :[[#@LINE+1]]:12: warning: the prologue said r40
	.save rp, r50
	nop.i 0
// WARN: :[[#@LINE+1]]:16: warning: the prologue said r41
	.save ar.pfs, r50
	nop.i 0
// WARN: :[[#@LINE+1]]:10: warning: the prologue said r42
	.vframe r127
	nop.i 0
// WARN: :[[#@LINE+1]]:12: warning: the prologue said r43
	.save pr, r50
	nop.i 0
	.body
	br.ret.sptk.many rp
	.endp f

// CHECK:      0x00000000 02000000 00000100 47a806e4 01e602e1
// CHECK-NEXT: 0x00000010 04e80523 00000000
