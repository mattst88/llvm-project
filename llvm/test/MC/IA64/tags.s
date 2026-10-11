// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -s -r -x .data %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// A tag is a symbol for one instruction: the address of the bundle that
// the instruction comes to be in, and in the low bits which slot of it. It
// does not end the bundle as a label does. GNU as writes the same object
// for this file.

	.text
// ASM:      [.La:]
// ASM-NEXT: nop 0
[.La:]	nop 0
[.Lb:]
	nop 1
// Any number of them, and a predicate after them.
// ASM:      [c:]
// ASM-NEXT: [d:]
// ASM-NEXT: (p6) nop 2
[c:] [d:] (p6) nop 2
	;;
// An instruction of two slots has the first.
[.Le:]	movl r8 = 0x123456789
[f:]	nop 3
// A label is on a bundle of its own, and the tag after it in that.
g:
[h:]	nop 4
// Directives may come between it and the instruction.
[i:]
	.mii
	nop 5
// The mnemonic after a tag is in either case.
// ASM:      [.Lm:]
// ASM-NEXT: br.ret.sptk.many b0
[.Lm:]	BR.RET.SPTK.MANY b0
// With no instruction for it a tag is a label.
[j:]
	.data
[k:]	data8 .La, .Lb, .Le
	data1 .Lb - .La, c - .La, .Le - .La, f - .Le, h - .La, h - g, i - h
	.uleb128 i - .La
	.global d
[l:]
// A label after a tag does not take the instruction from it.
	.section .text.b, "ax"
	nop 6
[m:]
n:
	br.ret.sptk.many b0

// CHECK:      Relocation section '.rela.data'
// CHECK:      0000000000000000 {{.*}} R_IA64_DIR64LSB {{.*}} .text + 0
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_DIR64LSB {{.*}} .text + 1
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_DIR64LSB {{.*}} .text + 11

// CHECK:      0000000000000002 0 NOTYPE LOCAL  DEFAULT [[#TEXT:]] c
// CHECK-NEXT: 0000000000000020 0 NOTYPE LOCAL  DEFAULT [[#TEXT]] f
// CHECK-NEXT: 0000000000000030 0 NOTYPE LOCAL  DEFAULT [[#TEXT]] g
// CHECK-NEXT: 0000000000000030 0 NOTYPE LOCAL  DEFAULT [[#TEXT]] h
// CHECK-NEXT: 0000000000000040 0 NOTYPE LOCAL  DEFAULT [[#TEXT]] i
// CHECK-NEXT: 0000000000000060 0 NOTYPE LOCAL  DEFAULT [[#TEXT]] j
// CHECK-NEXT: 0000000000000000 0 NOTYPE LOCAL  DEFAULT [[#DATA:]] k
// CHECK-NEXT: 0000000000000020 0 NOTYPE LOCAL  DEFAULT [[#DATA]] l
// CHECK-NEXT: 0000000000000010 0 NOTYPE LOCAL  DEFAULT [[#TEXTB:]] n
// CHECK-NEXT: 0000000000000012 0 NOTYPE LOCAL  DEFAULT [[#TEXTB]] m
// CHECK-NEXT: 0000000000000002 0 NOTYPE GLOBAL DEFAULT [[#TEXT]] d

// CHECK:      Hex dump of section '.data':
// CHECK-NEXT: 0x00000000 00000000 00000000 00000000 00000000
// CHECK-NEXT: 0x00000010 00000000 00000000 0102110f 30001040
