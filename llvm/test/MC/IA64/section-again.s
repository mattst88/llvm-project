// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d -z %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// A directive that selects a section ends the instruction group and the
// bundle, as it does for GNU as, also when the section it selects is the
// current one already. The assembly that is printed has the directive for
// the section where the source had one that changed nothing.

// ASM:      .text
// ASM-NEXT: f:
	.text
f:
// CHECK:       0: {{.*}} nop.m 0 // [MII]
// CHECK-NEXT:  6: {{.*}} nop.i 1
// CHECK-NEXT:  c: {{.*}} nop.i 0 ;;
// ASM:      nop 1
// ASM-NEXT: .text
// ASM-NEXT: nop 2
	nop 0
	nop 1
	.text
// CHECK-NEXT: 10: {{.*}} nop.m 2 // [MII]
// CHECK-NEXT: 16: {{.*}} nop.i 0
// CHECK-NEXT: 1c: {{.*}} nop.i 0 ;;
	nop 2
	.pushsection .text
// CHECK-NEXT: 20: {{.*}} nop.m 3 // [MII]
// CHECK-NEXT: 26: {{.*}} nop.i 0
// CHECK-NEXT: 2c: {{.*}} nop.i 0 ;;
	nop 3
	.popsection
// CHECK-NEXT: 30: {{.*}} nop.m 4 // [MII]
// CHECK-NEXT: 36: {{.*}} nop.i 0
// CHECK-NEXT: 3c: {{.*}} nop.i 0 ;;
	nop 4
	.section .text
// CHECK-NEXT: 40: {{.*}} nop.m 5 // [MII]
// CHECK-NEXT: 46: {{.*}} nop.i 0
// CHECK-NEXT: 4c: {{.*}} nop.i 0 ;;
	nop 5
	.subsection 0
// CHECK-NEXT: 50: {{.*}} nop.m 6 // [MII]
// CHECK-NEXT: 56: {{.*}} nop.i 0
// CHECK-NEXT: 5c: {{.*}} nop.i 0 ;;
	nop 6
	.data
	.previous
	.previous
	.previous
// CHECK-NEXT: 60: {{.*}} nop.m 7 // [MII]
// CHECK-NEXT: 66: {{.*}} nop.i 8
// CHECK-NEXT: 6c: {{.*}} nop.i 9 ;;
	nop 7
	nop 8
	nop 9
