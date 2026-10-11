// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -s -j .text - \
// RUN:   | FileCheck %s

// Data ends the instruction group and the bundle. Code alignment pads what
// does not fill a bundle with zeros and the rest with nop bundles. GNU as
// writes the same bytes.

	.text
	add r10 = r2, r3
	.byte 0x55
	.align 16
	add r11 = r2, r3
	.byte 0x66
	.align 64
	add r12 = r2, r3

// CHECK:      0000 01500806 00200000 00020000 00000400
// CHECK-NEXT: 0010 55000000 00000000 00000000 00000000
// CHECK-NEXT: 0020 01580806 00200000 00020000 00000400
// CHECK-NEXT: 0030 66000000 00000000 00000000 00000000
// CHECK-NEXT: 0040 01600806 00200000 00020000 00000400
