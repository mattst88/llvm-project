// RUN: llvm-mc -triple=ia64 %s | FileCheck %s

// The names GNU as has for numbers, besides the classes of fclass: the
// features that tf tests for, the hints, the old name of the NaT class, and
// the operating systems.

// CHECK:      tf.z p6, p7 = 32
// CHECK-NEXT: tf.z p6, p7 = 33
// CHECK-NEXT: tf.z p6, p7 = 34
	tf.z p6, p7 = @clz
	tf.z p6, p7 = @mpy
	tf.z p6, p7 = @datahints

// CHECK:      hint 0
// CHECK-NEXT: hint 1
	hint @pause
	hint @priority

// CHECK:      fclass.m p6, p7 = f8, 256
	fclass.m p6, p7 = f8, @natval

// CHECK:      mov r4 = 0
// CHECK-NEXT: mov r4 = 1
// CHECK-NEXT: mov r4 = 2
// CHECK-NEXT: mov r4 = 3
// CHECK-NEXT: mov r4 = 9
// CHECK-NEXT: mov r4 = 13
// CHECK-NEXT: mov r4 = 14
	mov r4 = @svr4
	mov r4 = @hpux
	mov r4 = @nt
	mov r4 = @linux
	mov r4 = @freebsd
	mov r4 = @openvms
	mov r4 = @nsk
