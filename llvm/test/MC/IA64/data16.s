// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r -x .data %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2
// RUN: not llvm-mc -triple ia64 -filetype=obj --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR
// RUN: not llvm-mc -triple ia64 -filetype=obj --defsym ERR=2 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR2

// data16 is sixteen bytes, aligned as the smaller ones are unless it ends
// in ".ua". The one relocation of that size is "@iplt", which has the
// dynamic linker put the address and the gp of a function there: GCC
// fills a virtual table with these. GNU as writes the same object for this
// file.

	.data
	data8 1
// ASM:      data16.ua @iplt(f)
// ASM-NEXT: .p2align 4
// ASM-NEXT: data16.ua @iplt(f)
x:	data16.ua @iplt(f#)
	data16 @iplt(f#)
	data16 @iplt(f# + 16)
// ASM:      data16.ua @iplt(x)
// ASM-NEXT: data16.ua @iplt(g)
	data16 @iplt(x), @iplt(g)
	data16 0x112233445566778899aabbccddeeff00, 1, -1
	data1 1
	data16.ua -2, 3
	.text
f:	nop 0

// CHECK:      .rela.data {
// CHECK-NEXT:   0x8 R_IA64_IPLTLSB .text 0x0
// CHECK-NEXT:   0x20 R_IA64_IPLTLSB .text 0x0
// CHECK-NEXT:   0x30 R_IA64_IPLTLSB .text 0x10
// CHECK-NEXT:   0x40 R_IA64_IPLTLSB .data 0x8
// CHECK-NEXT:   0x50 R_IA64_IPLTLSB g 0x0
// CHECK-NEXT: }

// CHECK:      0x00000060 00ffeedd ccbbaa99 88776655 44332211
// CHECK-NEXT: 0x00000070 01000000 00000000 00000000 00000000
// CHECK-NEXT: 0x00000080 ffffffff ffffffff ffffffff ffffffff
// CHECK-NEXT: 0x00000090 01feffff ffffffff ffffffff ffffffff
// CHECK-NEXT: 0x000000a0 ff030000 00000000 00000000 00000000
// CHECK-NEXT: 0x000000b0 00

.ifdef ERR
.if ERR == 1
	.data
// ERR: :[[#@LINE+1]]:9: error: expected a number or '@iplt'
	data16 f
// ERR: :[[#@LINE+1]]:9: error: expected a number or '@iplt'
	data16 @iplt(f) + 1
// ERR: :[[#@LINE+1]]:9: error: expected a number or '@iplt'
	data16 @ltoff(f)
.else
	.data
// ERR2-DAG: :[[#@LINE+1]]:8: error: relocation specifier is not supported here
	data8 @iplt(f)
	.text
// ERR2-DAG: :[[#@LINE+1]]:12: error: relocation specifier is not supported here
	addl r2 = @iplt(f), r1
.endif
.endif
