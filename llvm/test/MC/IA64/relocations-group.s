// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=B
// RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=T
// RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=D

// A relocation against a local symbol of a section that is in a group is
// against the symbol, as GNU as has it: the group may be discarded, and
// the name is what tells of it. A symbol that has no name to keep still
// gives way to its section.

	.section .text.a,"axG",@progbits,ga,comdat
la:
	nop 0
	;;
.La:
	nop 0
	;;

	.section .text.b,"axG",@progbits,gb,comdat
// B:      .rela.text.b {
// B-NEXT:   0x0 R_IA64_PCREL21B la 0x0
// B-NEXT:   0x1 R_IA64_PCREL21B .text.a 0x10
// B-NEXT:   0x2 R_IA64_PCREL21B .text 0x0
// B-NEXT: }
	br.call.sptk.many b0 = la
	br.call.sptk.many b0 = .La
	br.call.sptk.many b0 = lc
	;;

	.text
lc:
// T:      .rela.text {
// T-NEXT:   0x0 R_IA64_LTOFF22 la 0x0
// T-NEXT:   0x1 R_IA64_LTOFF22 .text.a 0x10
// T-NEXT: }
	addl r14 = @ltoff(la), r1
	addl r14 = @ltoff(.La), r1

	.data
// D:      .rela.data {
// D-NEXT:   0x0 R_IA64_DIR64LSB la 0x4
// D-NEXT:   0x8 R_IA64_DIR64LSB .text.a 0x10
// D-NEXT:   0x10 R_IA64_DIR64LSB .text 0x0
// D-NEXT: }
	data8 la + 4, .La, lc
