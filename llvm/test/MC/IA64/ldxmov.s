// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -dr --no-show-raw-insn %t | FileCheck %s
// RUN: llvm-mc -triple ia64 -show-encoding %s | FileCheck %s --check-prefix=ENC

// "ld8.mov" is the load of an address from the linkage table that the linker
// may turn into a move: a plain ld8 with a relocation that names the symbol
// whose address it loads. A local symbol is its section there, and a constant
// leaves the load alone. GNU as writes the same object for this file.

	.text
f:
// CHECK:      ld8 r15 = [r15]
// CHECK-NEXT:   R_IA64_LDXMOV x
// CHECK-NEXT:   R_IA64_LDXMOV x+0x8
// CHECK-NEXT: ld8 r16 = [r17]
// CHECK-NEXT: nop.i 0
// CHECK-NEXT: (p6) ld8 r16 = [r17] ;;
// CHECK-NEXT:   R_IA64_LDXMOV .text
// CHECK-NEXT:   R_IA64_LDXMOV .data+0x4
// CHECK-NEXT: ld8 r16 = [r17]
// CHECK-NEXT: nop.i 0
// CHECK-NEXT: ld8 r1 = [r2]
// ENC:      ld8.mov r15 = [r15], x // encoding: [0xc0,0x03,0xf0,0xc0,0x80,0x00]
// ENC-NEXT: fixup A - offset: 0, value: x, kind: fixup_ia64_ldxmov
// ENC:      ld8.mov r16 = [r17], x+8
// ENC:      (p6) ld8.mov r16 = [r17], f
// ENC:      ld8.mov r16 = [r17], d+4
// ENC:      ld8.mov r1 = [r2], 8 // encoding: [0x40,0x00,0x20,0xc0,0x80,0x00]
// ENC-NOT:  fixup
	ld8.mov r15 = [r15], x#
	ld8.mov r16 = [r17], x + 8
	(p6) ld8.mov r16 = [r17], f
	;;
	ld8.mov r16 = [r17], d + 4
	ld8.mov r1 = [r2], 8

	.data
d:
	.8byte 0
