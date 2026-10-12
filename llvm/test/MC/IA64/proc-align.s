// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-nm -n %t | FileCheck %s --check-prefix=NM
// RUN: llvm-objdump -s -j .text %t | FileCheck %s

// A procedure begins on a bundle: .proc pads the data before it, as it does
// for GNU as, and the label after it is at the bundle.

	.text
	data4.ua 1
	data4.ua 2
	.proc f
f:
	nop.i 0
	.endp f

// NM: 0000000000000010 t f

// CHECK:      0000 01000000 02000000 00000000 00000000
// CHECK-NEXT: 0010 01000000 01000000 00020000 00000400
