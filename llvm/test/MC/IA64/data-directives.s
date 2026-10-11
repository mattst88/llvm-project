// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -s -j .data %t | FileCheck %s
// RUN: llvm-readelf -S -s %t | FileCheck %s --check-prefix=ELF
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj \
// RUN:   | llvm-objdump -s -j .data - | FileCheck %s
// RUN: llvm-objdump -s -j .dc %t | FileCheck %s --check-prefix=DC
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj \
// RUN:   | llvm-objdump -s -j .dc - | FileCheck %s --check-prefix=DC

// The data directives of GNU as have no dot. data2, data4 and data8 align
// what they write to its size, and so do .short, .long and .quad, which is
// not what they do for other targets. The ".ua" forms and the directives
// naming a number of bytes do not align. GNU as writes the same bytes.

	.data
	data1 1
// A label ahead of the padding stays where it is.
lab:
	data8 2, -1
	data1 3
	data2 4, 0xffff
	data1 5
	data4 lab2 - lab, -2147483648
	data1 6
lab2:
	data2.ua 7
	data4.ua 8
	data8.ua 9
	data1 10
	.short 11
	data1 12
	.long 13
	data1 14
	.quad 15
	data1 16
	.2byte 17
	.4byte 18
	.8byte 19
	string "ab"
	stringz "cd\n", "ef"

// CHECK:      0000 01000000 00000000 02000000 00000000
// CHECK-NEXT: 0010 ffffffff ffffffff 03000400 ffff0500
// CHECK-NEXT: 0020 28000000 00000080 06070008 00000009
// CHECK-NEXT: 0030 00000000 0000000a 0b000c00 0d000000
// CHECK-NEXT: 0040 0e000000 00000000 0f000000 00000000
// CHECK-NEXT: 0050 10110012 00000013 00000000 00000061
// CHECK-NEXT: 0060 6263640a 00656600

// .dc, .dc.w, .dc.l, .dc.a and .octa align as well, and .dc.b has nothing
// to align.
	.section .dc,"aw",@progbits
	data1 1
	.dc 2
	data1 3
	.dc.w 4, 5
	data1 6
	.dc.l 7
	data1 8
	.dc.a lab
	data1 9
	.dc.b 10
	.octa 11

// DC:      0000 01000200 03000400 05000600 07000000
// DC-NEXT: 0010 08000000 00000000 00000000 00000000
// DC-NEXT: 0020 090a0000 00000000 00000000 00000000
// DC-NEXT: 0030 0b000000 00000000 00000000 00000000

// The section is as aligned as its most aligned item.
	.section .two,"a",@progbits
	data1 1
	data2 2
	.section .four,"a",@progbits
	data1 1
	data4 2
	.section .unaligned,"a",@progbits
	data1 1
	data8.ua 2

// ELF: .data      PROGBITS {{.*}} WA 0 0 8
// ELF: .two       PROGBITS {{.*}} A 0 0 2
// ELF: .four      PROGBITS {{.*}} A 0 0 4
// ELF: .unaligned PROGBITS {{.*}} A 0 0 1
// ELF: 0000000000000001 0 NOTYPE LOCAL DEFAULT {{.*}} lab
// ELF: 0000000000000029 0 NOTYPE LOCAL DEFAULT {{.*}} lab2
