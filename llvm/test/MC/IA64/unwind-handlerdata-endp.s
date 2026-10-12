// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -S -s %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.3
// RUN: cmp %t %t.3

// A procedure may end in the data for its handler. The end is where the code
// stopped, and what follows is in the section of the code again, as it is for
// GNU as. The assembly
// that is printed names that section, and makes the same object.

	.section .text.a,"ax",@progbits
	.proc f1
f1:
	.personality routine
	.prologue
	.fframe 16
	nop 0
	.body
	br.ret.sptk.many b0
// ASM:      .handlerdata
// ASM:      data4.ua 7
// ASM-NEXT: .section .text.a,"ax",@progbits
// ASM-NEXT: .endp
	.handlerdata
	data4 7
	.endp f1
after:
	nop 0

// CHECK: .text.a PROGBITS 0000000000000000 {{[0-9a-f]+}} 000020
// CHECK: 0000000000000000 16 FUNC LOCAL DEFAULT [[#]] f1
// CHECK: 0000000000000010 0 NOTYPE LOCAL DEFAULT [[#]] after
