// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -S - | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj \
// RUN:   | llvm-readelf -S - | FileCheck %s

// The "o" flag needs no symbol. An unwind table written by hand is ordered
// with .text, whatever it is called and whether or not it says "o". A
// section with the name of an unwind table is one, whatever else the
// directive says or does not say of it; one of unwind information is not.

// ASM: .section .IA_64.unwind,"ao",@unwind,.text
// ASM: .section .IA_64.unwind.text.f,"ao",@unwind,.text
// ASM: .section .table,"ao",@unwind,.text
// ASM: .section .ordered,"ao",@progbits,0
// ASM: .section .with,"ao",@progbits,d
// ASM: .section .table.with,"ao",@unwind,d
// ASM: .section .IA_64.unwind.bare,"o",@unwind,.text
// ASM: .section .gnu.linkonce.ia64unw.g,"o",@unwind,.text
// ASM: .section .IA_64.unwindx,"ao",@unwind,.text
// ASM: .section .IA_64.unwind_info.bare,"",@progbits
// ASM: .section .gnu.linkonce.ia64unwi.g,"",@progbits

// CHECK: [ [[#%d,TEXT:]]] .text       PROGBITS
// CHECK: [ [[#%d,DATA:]]] .data       PROGBITS
// CHECK: .IA_64.unwind        IA_64_UNWIND {{[0-9a-f ]+}} AL [[#TEXT]] 0 8
// CHECK: .IA_64.unwind.text.f IA_64_UNWIND {{[0-9a-f ]+}} AL [[#TEXT]] 0 8
// CHECK: .table               IA_64_UNWIND {{[0-9a-f ]+}} AL [[#TEXT]] 0 8
// CHECK: .ordered             PROGBITS     {{[0-9a-f ]+}} AL 0 0 8
// CHECK: .with                PROGBITS     {{[0-9a-f ]+}} AL [[#DATA]] 0 8
// CHECK: .table.with          IA_64_UNWIND {{[0-9a-f ]+}} AL [[#DATA]] 0 8
// CHECK: .IA_64.unwind.bare   IA_64_UNWIND {{[0-9a-f ]+}} L [[#TEXT]] 0 8
// CHECK: .gnu.linkonce.ia64unw.g IA_64_UNWIND {{[0-9a-f ]+}} L [[#TEXT]] 0 8
// CHECK: .IA_64.unwindx       IA_64_UNWIND {{[0-9a-f ]+}} AL [[#TEXT]] 0 8
// CHECK: .IA_64.unwind_info.bare PROGBITS  {{[0-9a-f ]+}} 00 0 0 8
// CHECK: .gnu.linkonce.ia64unwi.g PROGBITS {{[0-9a-f ]+}} 00 0 0 8

	.data
d:	data8 0
	.section .text.f,"ax",@progbits
	nop 0
	.section .IA_64.unwind, "ao", "unwind"
	data8 0
	.section .IA_64.unwind.text.f,"ao",@unwind
	data8 0
	.section .table,"a",@unwind
	data8 0
	.section .ordered,"ao",@progbits
	data8 0
	.section .with,"ao",@progbits,d
	data8 0
	.section .table.with,"ao",@unwind,d
	data8 0
	.section .IA_64.unwind.bare
	data8 0
	.section .gnu.linkonce.ia64unw.g
	data8 0
	.section .IA_64.unwindx,"a",@progbits
	data8 0
	.section .IA_64.unwind_info.bare
	data8 0
	.section .gnu.linkonce.ia64unwi.g
	data8 0
