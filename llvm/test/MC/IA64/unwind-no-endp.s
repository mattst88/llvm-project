// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t 2>&1 | FileCheck %s --check-prefix=WARN
// RUN: llvm-readelf -x .IA_64.unwind_info -r %t | FileCheck %s

// A procedure that is not ended has no unwind information, and what it said
// of its regions is not taken for the next procedure's.

// WARN: :[[#@LINE+18]]:8: warning: the previous .proc has no .endp

// CHECK:      Relocation section '.rela.IA_64.unwind' at offset {{.*}} contains 3 entries:
// CHECK-NEXT:     Offset
// CHECK-NEXT: 0000000000000000 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 10
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 20
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info + 0

// CHECK:      Hex dump of section '.IA_64.unwind_info':
// CHECK-NEXT: 0x00000000 01000000 00000100 44200300 00000000

	.text
	.proc a
a:
	.prologue 12, r40
	nop 0
	.body
	nop 0
	.proc b
b:
	.prologue 8, r32
	nop 0
	.endp b
