// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -S - | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM

// "s" is for a section of small data, which is addressed from gp.

// ASM: .section .sdata,"aws",@progbits
// ASM: .section .sbss,"aws",@nobits
// ASM: .section .srodata,"as",@progbits

// CHECK: .sdata   PROGBITS {{.*}} WAp 0 0 8
// CHECK: .sbss    NOBITS   {{.*}} WAp 0 0 1
// CHECK: .srodata PROGBITS {{.*}} Ap  0 0 8

	.section .sdata,"aws",@progbits
	data8 0
	.section .sbss,"aws",@nobits
	.skip 8
	.section .srodata,"as",@progbits
	data8 0
