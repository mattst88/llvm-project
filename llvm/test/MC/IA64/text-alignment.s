// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -S %t | FileCheck %s
// RUN: llvm-mc -triple ia64 -filetype=obj /dev/null -o %t.2
// RUN: llvm-readelf -S %t.2 | FileCheck %s --check-prefix=EMPTY

// GNU as has .text be as aligned as a bundle is with no instruction in it,
// and with nothing in it at all. Another section of code is as aligned as
// what is in it.

	.text
	data1 1
	.section .text.other,"ax",@progbits
	data1 1

// CHECK: .text       PROGBITS {{.*}} AX 0 0 16
// CHECK: .text.other PROGBITS {{.*}} AX 0 0 1
// EMPTY: .text       PROGBITS {{.*}} AX 0 0 16
