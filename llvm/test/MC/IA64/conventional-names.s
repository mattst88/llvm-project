// RUN: llvm-mc -triple=ia64 %s | FileCheck %s

// The registers of a result and of the floating-point arguments have names
// for what they hold. The names are in lower case only and stop where the
// registers do: anything else is a symbol.

// CHECK:      mov r8 = r9
// CHECK-NEXT: mov r10 = r11
	mov	ret0 = ret1
	mov	ret2 = ret3
// CHECK-NEXT: fma f8 = f9, f1, f15
// CHECK-NEXT: fma f8 = f9, f1, f15
	fadd	farg0 = farg1, farg7
	fadd	fret0 = fret1, fret7
// CHECK-NEXT: mov r8 = ret4
// CHECK-NEXT: mov r8 = RET0
// CHECK-NEXT: mov r8 = farg8
// CHECK-NEXT: mov r8 = ret01
	mov	r8 = ret4
	mov	r8 = RET0
	mov	r8 = farg8
	mov	r8 = ret01
// A symbol of one of the names says that it is one.
// CHECK-NEXT: mov r8 = ret0#
	mov	r8 = ret0#
