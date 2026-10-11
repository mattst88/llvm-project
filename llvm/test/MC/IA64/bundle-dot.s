// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d -z %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// "." in the target of a branch is the bundle that the branch comes to be
// in. It is not a label, which would end the bundle that is being filled.

	.text
	nop 0
	nop 0
	nop 0
	nop 0
// ASM: br.cond.sptk.few .+32
	br.cond.sptk .+32
	nop 1
	mov b0 = r2, .+32
// ASM: br.call.sptk.few b0 = .+0
	br.call.sptk b0 = .
// ASM: brp.sptk .-16, .+16
	brp.sptk .-16, . + 16
	chk.s r4, .+0x20
	brl.cond.sptk .-0x30

// CHECK:       0: {{.*}} nop.m 0 // [MII]
// CHECK:      10: {{.*}} nop.m 0 // [MBB]
// CHECK-NEXT: 16: {{.*}} br.cond.sptk.few 0x30
// CHECK-NEXT: 1c: {{.*}} nop.b 1
// CHECK-NEXT: 20: {{.*}} nop.m 0 // [MIB]
// CHECK-NEXT: 26: {{.*}} mov b0 = r2, 0x40
// CHECK-NEXT: 2c: {{.*}} br.call.sptk.few b0 = 0x20
// CHECK-NEXT: 30: {{.*}} nop.m 0 // [MBB]
// CHECK-NEXT: 36: {{.*}} brp.sptk 0x20, 0x40
// CHECK-NEXT: 3c: {{.*}} nop.b 0
// CHECK-NEXT: 40: {{.*}} chk.s.m r4, 0x60 // [MLX]
// CHECK-NEXT: 46: {{.*}} brl.cond.sptk.few 0x10 ;;
