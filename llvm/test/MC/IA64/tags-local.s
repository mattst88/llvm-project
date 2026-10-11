// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r -x .data %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// A tag may be a local label of a number, which is found forwards before the
// instruction and backwards after it, as far as the next tag or label of
// that number. GNU as writes the same object for this file.

	.text
f:
	nop 0
	br.sptk.many 1f
[1:]	nop 1
	.data
// The first is in slot 2 of the first bundle.
// CHECK:      0x0 R_IA64_DIR64LSB .text 0x2
	data8 1b
	.text
[1:]	nop 2
// A branch to a slot is one to its bundle.
	br.sptk.many 1b
	;;

	.data
// The second is in slot 0 of the bundle after it.
// CHECK-NEXT: 0x8 R_IA64_DIR64LSB .text 0x10
	data8 1b
1:
// CHECK-NEXT: 0x10 R_IA64_DIR64LSB .data 0x10
	data8 1b
