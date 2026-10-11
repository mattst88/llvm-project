// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r %t | FileCheck %s
// RUN: llvm-readelf -s %t | FileCheck %s --check-prefix=SYM
// RUN: llvm-mc -triple ia64 %s -o %t.s
// RUN: FileCheck %s --check-prefix=ASM --input-file=%t.s
// RUN: llvm-mc -triple ia64 -filetype=obj %t.s -o %t2
// RUN: cmp %t %t2

// "name#" is the symbol and never a register, which is how GCC writes every
// symbol. The '#' is no part of the name.

	.text
	.global f#
	.type f#, @function
	.hidden f#
	.weak w#
f:
// CHECK:      0x0 R_IA64_LTOFF22 r4 0x0
	addl r14 = @ltoff(r4#), r1
// CHECK-NEXT: 0x1 R_IA64_GPREL22 r4 0x8
	addl r14 = @gprel(r4# + 8), r1
// CHECK-NEXT: 0x2 R_IA64_LTOFF_FPTR22 w 0x0
	addl r15 = @ltoff(@fptr(w#)), r1
// CHECK-NEXT: 0x12 R_IA64_PCREL21B b6 0x0
	br.call.sptk.many b0 = b6#
// CHECK-NEXT: 0x21 R_IA64_IMM64 r4 0x0
	movl r14 = r4#
// CHECK-NEXT: 0x30 R_IA64_IMM14 r4 0x0
	adds r14 = r4#, r4
.Lend:
	.size f#, .Lend - f#

	.data
	.global r4#
	.type r4#, @object
	.size r4#, 16
r4:
// CHECK:      0x0 R_IA64_DIR64LSB r4 0x0
	data8 r4#
// CHECK-NEXT: 0x8 R_IA64_FPTR64LSB f 0x0
	data8 @fptr(f#)
	data8.ua in0#, loc95#, out0# + 4, sp#, ar.pfs#, cr.ifa#, psr.l#, farg7#
	data8.ua r128, in96, r04, b8
	.comm c#, 8, 8

// SYM-DAG: 64 FUNC   GLOBAL HIDDEN  {{.*}} f
// SYM-DAG: 16 OBJECT GLOBAL DEFAULT {{.*}} r4
// SYM-DAG:  8 OBJECT GLOBAL DEFAULT COM c
// SYM-DAG:  0 NOTYPE WEAK   DEFAULT UND w
// SYM-DAG:  0 NOTYPE GLOBAL DEFAULT UND b6

// A name that an expression would read as a register is printed with its '#',
// and no other name is.
// ASM:      .globl f
// ASM:      addl r14 = @ltoff(r4#), r1
// ASM-NEXT: addl r14 = @gprel(r4#+8), r1
// ASM-NEXT: addl r15 = @ltoff(@fptr(w)), r1
// ASM:      br.call.sptk.many b0 = b6#
// ASM:      movl r14 = r4#
// ASM:      .size f, .Lend-f
// ASM:      r4:
// ASM:      data8.ua r4#
// ASM:      data8.ua in0#
// ASM-NEXT: data8.ua loc95#
// ASM-NEXT: data8.ua out0#+4
// ASM-NEXT: data8.ua sp#
// ASM-NEXT: data8.ua ar.pfs#
// ASM-NEXT: data8.ua cr.ifa#
// ASM-NEXT: data8.ua psr.l#
// ASM-NEXT: data8.ua farg7#
// ASM-NEXT: data8.ua r128
// ASM-NEXT: data8.ua in96
// ASM-NEXT: data8.ua r04
// ASM-NEXT: data8.ua b8
