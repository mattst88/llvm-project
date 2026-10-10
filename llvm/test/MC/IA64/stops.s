// A ";;" ends an instruction group, whether it follows an instruction or
// stands on a line of its own. A lone ";" only separates statements.

// RUN: llvm-mc -triple ia64 %s | FileCheck %s

// CHECK:      add r1 = r2, r3
// CHECK-NEXT: ;;
// CHECK:      add r4 = r1, r1
// CHECK-NEXT: add r5 = r1, r1
// CHECK-NEXT: ;;
// CHECK:      (p6) sub r6 = r4, r5
// CHECK-NEXT: (p7) sub r6 = r5, r4
// CHECK-NEXT: ;;
// CHECK:      or r8 = r6, r0
// CHECK-NOT:  ;;
	add r1 = r2, r3 ;;
	add r4 = r1, r1
	add r5 = r1, r1
	;;
	(p6) sub r6 = r4, r5 ; (p7) sub r6 = r5, r4;;
	or r8 = r6, r0
