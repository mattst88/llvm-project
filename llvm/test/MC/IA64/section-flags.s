// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -S - | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM

// "s" is for a section of small data, which is addressed from gp.

// ASM: .section .sdata,"aws",@progbits
// ASM: .section .sbss,"aws",@nobits
// ASM: .section .srodata,"as",@progbits

// CHECK: .sdata   PROGBITS {{.*}} WAp 0 0 8
// CHECK: .sbss    NOBITS   {{.*}} WAp 0 0 1
// CHECK: .srodata PROGBITS {{.*}} Ap  0 0 8
// CHECK: .sdata.a PROGBITS {{.*}} WAp 0 0 1
// CHECK: .sbss.a  NOBITS   {{.*}} WAp 0 0 1
// CHECK: .sdata2  PROGBITS {{.*}} WAp 0 0 1
// CHECK: .sbss2   NOBITS   {{.*}} WAp 0 0 1
// CHECK: .sdat    PROGBITS {{[0-9a-f ]+}} 0 0 1

	.section .sdata,"aws",@progbits
	data8 0
	.section .sbss,"aws",@nobits
	.skip 8
	.section .srodata,"as",@progbits
	data8 0

// Their names say as much, and they have directives of their own. All of
// one directive is one section.

// ASM:      .section .sdata,"aws",@progbits
// ASM:      data1 1
// ASM:      .section .sbss,"aws",@nobits
// ASM:      .text
// ASM:      .section .sdata,"aws",@progbits
// ASM:      data1 2
// ASM:      .section .sdata.a,"aws",@progbits
// ASM:      .section .sbss.a,"aws",@nobits
// ASM:      .section .sdata2,"aws",@progbits
// ASM:      .section .sbss2,"aws",@nobits
// ASM:      .section .sdat,"",@progbits

	.sdata
	data1 1
	.sbss
	.skip 1
	.previous
	.previous
	.skip 1
	.text
	.SDATA
	data1 2
	.SBSS
	.section .sdata.a
	data1 0
	.section .sbss.a
	.skip 1
	.section .sdata2
	data1 0
	.section .sbss2
	.skip 1
	.section .sdat
	data1 0
