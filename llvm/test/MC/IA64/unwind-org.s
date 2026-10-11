// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s

// The unwind information counts the slots between the instructions of a
// procedure, and they can be counted past a .org that says where the
// procedure is: at a place that is a number, or at a distance from a symbol
// whose own place is known. GNU as writes the same object for this file.

	.text
	.align 32
base:
	mov r2 = r3
	br.ret.sptk b0 ;;
	.org base + 0x100
	.proc f
f:
	.prologue
	.save ar.pfs, r34
	alloc r34 = ar.pfs, 0, 3, 0, 0
	.save rp, r33
	mov r33 = b0
	.body
	mov b0 = r33
	br.ret.sptk b0 ;;
	.endp f
	.org 0x200
	.proc g
g:
	.prologue
	.fframe 16
	adds r12 = -16, r12
	.body
	nop.i 0
	.restore sp
	adds r12 = 16, r12
	br.ret.sptk b0 ;;
	.endp g

// CHECK:      Hex dump of section '.IA_64.unwind_info':
// CHECK-NEXT: 0x00000000 02000000 00000100 02e600b1 22e401b0
// CHECK-NEXT: 0x00000010 a1240000 00000000 01000000 00000100
// CHECK-NEXT: 0x00000020 01e00001 25c00300
