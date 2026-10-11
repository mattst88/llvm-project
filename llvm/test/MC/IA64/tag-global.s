// RUN: llvm-mc -triple=ia64 -filetype=obj %s | llvm-objdump -dr -z - \
// RUN:   | FileCheck %s

// A tag has no relocation, so one that names a global or a weak symbol of
// its own section is the distance to where the symbol is here, though another
// definition may take the symbol's place when the program is linked.

// The target of the branch that brp is about is relocated, as that of a
// branch is.

// CHECK-LABEL: <g>:
// CHECK:      mov.sptk b7 = r8, 0x0
// CHECK-NEXT: mov b6 = r2, 0x30
// CHECK-NEXT: nop.m 0
// CHECK-NEXT: R_IA64_PCREL21B w
// CHECK-NEXT: brp.sptk 0x20, 0x0
// CHECK-NEXT: br.cond.sptk.many b7 ;;
// CHECK-NOT:  R_IA64

	.text
	.global g
g:
	nop.m 0
	nop.m 0
	nop.i 0 ;;
	mov.sptk b7 = r8, g
	mov b6 = r2, w
	brp.sptk w, g
	br.many b7 ;;
	.weak w
w:
	br.ret.sptk.many b0 ;;
