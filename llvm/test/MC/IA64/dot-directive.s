// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d -z %t | FileCheck %s
// RUN: llvm-readelf -s -x .data %t | FileCheck %s --check-prefix=SYM
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// "." in a directive is where the next bundle begins: the bundle that is
// being filled ends there, with no stop, as it does at a label.
//
// This is not what GNU as does, and is not meant to be. GNU as holds the
// instructions it has read in a queue and writes a bundle some instructions
// later, and its "." is the end of what it has written, which is behind the
// instructions by as much as is in the queue. For the two procedures here it
// gives f the size 0 and g the size 16, and makes "here" the address of f.
// ".size f, . - f" after the last instruction is how glibc ends a procedure,
// and the size wanted is the one this gives.

	.text
f:
	nop 0
	br.ret.sptk.many b0
// ASM:      .Ltmp0:
// ASM-NEXT: .size f, .Ltmp0-f
	.size f, . - f
g:
	nop 1
	.equ here, .
	nop 2
	nop 3
	nop 4
	nop 5
	.size g, . - g
	.data
	data8 here - g

// CHECK:       0: {{.*}} nop.m 0 // [MIB]
// CHECK-NEXT:  6: {{.*}} nop.i 0
// CHECK-NEXT:  c: {{.*}} br.ret.sptk.many b0{{$}}
// CHECK:      10: {{.*}} nop.m 1 // [MII]
// CHECK-NEXT: 16: {{.*}} nop.i 0
// CHECK-NEXT: 1c: {{.*}} nop.i 0{{$}}
// CHECK:      20: {{.*}} nop.m 2 // [MII]
// CHECK-NEXT: 26: {{.*}} nop.i 3
// CHECK-NEXT: 2c: {{.*}} nop.i 4{{$}}
// CHECK-NEXT: 30: {{.*}} nop.m 5 // [MII]
// CHECK-NEXT: 36: {{.*}} nop.i 0
// CHECK-NEXT: 3c: {{.*}} nop.i 0 ;;

// SYM-DAG: 0000000000000000 16 NOTYPE LOCAL DEFAULT [[#]] f
// SYM-DAG: 0000000000000010 48 NOTYPE LOCAL DEFAULT [[#]] g
// SYM-DAG: 0000000000000020  0 NOTYPE LOCAL DEFAULT [[#]] here
// SYM: 0x00000000 10000000 00000000
