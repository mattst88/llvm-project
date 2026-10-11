// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// What the unwind information has of a body: where it restores sp, and
// which state it begins in when that is not the one the code before it
// ends in. Each procedure but the last has two words, the header and then
// descriptors padded with zeroes. GNU as writes the same object for this
// file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// Where sp is restored: the time, in slots back from the last one of the body.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0125c003 00000000
	.proc f1
f1:
	.prologue
	nop 0
	.body
	nop 0
	.restore sp
	nop 0
	nop 0
	br.ret.sptk.many b0
	.endp f1
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0161de02 c0950100
	.proc f2
f2:
	.prologue
	nop 0
	.body
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	.restore sp
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	.endp f2

// Restoring sp may undo more than the innermost prologue.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01210123 c1020000
	.proc f3
f3:
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	.restore sp, 1
	nop 0
	.endp f3

// A state that has a label can be had again, by code that is reached from
// where the label is and not from the code before it.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 012281c0 0123a100
	.proc f4
f4:
	.prologue
	nop 0
	.body
	.label_state 1
	.restore sp
	nop 0
	br.ret.sptk.many b0
	.body
	.copy_state 1
	nop 0
	.endp f4

// Labels from 32 on have a longer form.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01229ff0 20bff820
	.proc f5
f5:
	.prologue
	nop 0
	.body
	.label_state 31
	.label_state 32
	nop 0
	.copy_state 31
	.copy_state 32
	nop 0
	.endp f5
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 0122f0e8 07f8e807
	.proc f6
f6:
	.prologue
	nop 0
	.body
	.label_state 1000
	nop 0
	.copy_state 1000
	nop 0
	.endp f6

// And so do counts of prologues from 32 on.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 09000000 00000100 01210121 01210121
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01210121 01210121 01210121 01210121
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01210121 01210121 01210121 01210121
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01210121 01210121 01210121 01210121
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01210121 01210121 0124e002 20000000
	.proc f7
f7:
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.prologue
	nop 0
	.body
	nop 0
	.restore sp, 32
	nop 0
	.endp f7
