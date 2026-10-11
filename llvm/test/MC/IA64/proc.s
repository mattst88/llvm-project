// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -s %t | FileCheck %s --check-prefix=SYM
// RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM

// ".proc" and ".endp" bracket a procedure. Its entries are functions whose
// size is what there is from them to the end, unless the source gives one.
// Both end the bundle and the instruction group. GNU as writes the same
// object for this file.

// SYM-DAG: 0000000000000020 16 FUNC LOCAL  DEFAULT [[#]] g
// SYM-DAG: 0000000000000050 99 FUNC LOCAL  DEFAULT [[#]] h
// SYM-DAG: 0000000000000000 16 FUNC LOCAL  DEFAULT [[#]] j
// SYM-DAG: 0000000000000010 32 FUNC GLOBAL DEFAULT [[#]] f

	.text
// CHECK:      0: add r1 = r2, r3 // [MII]
// CHECK-NEXT:    nop.i 0
// CHECK-NEXT:    nop.i 0 ;;
	add r1 = r2, r3
// ASM:      .globl f
// ASM-NEXT: .proc f, g
// ASM-NEXT: f:
	.global f
	.proc f, g
f:
// CHECK:      10: add r4 = r1, r3 // [MII]
// CHECK-NEXT:     nop.i 0
// CHECK-NEXT:     nop.i 0{{$}}
	add r4 = r1, r3
g:
// CHECK:      20: add r5 = r1, r3 // [MII]
// CHECK-NEXT:     add r5 = r1, r3
// CHECK-NEXT:     nop.i 0 ;;
	add r5 = r1, r3
	add r5 = r1, r3
// ASM: .endp f, g
	.endp f, g
	add r5 = r1, r3
	add r5 = r1, r3
// CHECK:      30: add r5 = r1, r3 // [MII]
// CHECK-NEXT:     add r5 = r1, r3
// CHECK-NEXT:     nop.i 0 ;;
// CHECK:      40: add r6 = r1, r3 // [MII]
// CHECK:      50: add r6 = r1, r3 // [MII]
// ASM: .proc h
	.proc h
	add r6 = r1, r3
h:
	add r6 = r1, r3
	.size h, 99
// The names are optional.
// ASM: .endp h
	.ENDP

	.section .text.j,"ax",@progbits
	.PROC j
j:
	nop 0
	.endp j
