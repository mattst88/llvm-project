// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s

// A directive of a prologue is about the instruction after it. If the
// prologue has none after it, the time it gives is the end of the prologue
// and not the place of an instruction of the body, which for a prologue
// without instructions is the start of it. GNU as writes the same object for
// this file.

	.text
	.proc f
f:
	.prologue
	.body
	mov r2 = r3
	st8 [r24] = r9
	st8 [r25] = r10 ;;
	.prologue
	.fframe 464
	.spillsp rp, 72
	.body
	br.call.sptk.many rp = h ;;
	br.ret.sptk b0 ;;
	.endp f
	.proc g
g:
	.prologue
	mov r2 = r3
	mov r4 = r5
	mov r6 = r7
	mov r8 = r9 ;;
	.fframe 464
	.save rp, r2
	.body
	nop.i 0
	nop.i 0 ;;
	br.call.sptk.many rp = h ;;
	br.ret.sptk b0 ;;
	.endp g

// CHECK:      Hex dump of section '.IA_64.unwind_info':
// CHECK-NEXT: 0x00000000 02000000 00000100 002400e0 001df9e3
// CHECK-NEXT: 0x00000010 00122800 00000000 02000000 00000100
// CHECK-NEXT: 0x00000020 04e0041d e404b082 2b000000 00000000
