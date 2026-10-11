// RUN: llvm-mc -triple=ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s
// RUN: llvm-dwarfdump --debug-line %t | FileCheck %s --check-prefix=LINE

// A .loc is for the slot of the instruction after it and does not end the
// bundle. The address of a slot is that of its bundle with the number of
// the slot in the low bits.

	.file 1 "a.c"
	.text
	.proc f
f:
	.loc 1 1 1
	add r8 = r32, r33
	.loc 1 2 1
	add r9 = r32, r33
	.loc 1 3 1
	add r10 = r32, r33 ;;
// The first of two in a row is where the second is.
	.loc 1 4 1
	.loc 1 5 1
	add r11 = r32, r33
// A long instruction is in the second slot and takes the third too.
	.loc 1 6 1
	movl r14 = 0x123456789 ;;
	.loc 1 7 1
	br.ret.sptk.many b0 ;;
	.endp f

// CHECK:       0: add r8 = r32, r33 // [MII]
// CHECK-NEXT:  6: add r9 = r32, r33
// CHECK-NEXT:  c: add r10 = r32, r33 ;;
// CHECK-NEXT: 10: add r11 = r32, r33 // [MLX]
// CHECK-NEXT: 16: movl r14 = 4886718345 ;;
// CHECK-NEXT: 20: nop.m 0 // [MIB]
// CHECK-NEXT: 26: nop.i 0
// CHECK-NEXT: 2c: br.ret.sptk.many b0 ;;

// LINE:      0x0000000000000000      1      1
// LINE-NEXT: 0x0000000000000001      2      1
// LINE-NEXT: 0x0000000000000002      3      1
// LINE-NEXT: 0x0000000000000010      4      1
// LINE-NEXT: 0x0000000000000010      5      1
// LINE-NEXT: 0x0000000000000011      6      1
// LINE-NEXT: 0x0000000000000022      7      1
// LINE-NEXT: 0x0000000000000030      7      1 {{.*}} end_sequence
