// A ";" that is the first byte of the source has no byte before it that
// could make a stop of it. The source is large enough to be mapped, so that
// what is before it is not memory at all.

// RUN: %python -c "print(';\n;;\n' + '\tnop.i 0\n' * 9000)" > %t.s
// RUN: llvm-mc -triple ia64 %t.s | FileCheck %s

// CHECK-NOT:  ;;
// CHECK:      ;;
// CHECK-NOT:  ;;
// CHECK:      nop.i 0
// CHECK-NEXT: nop.i 0
