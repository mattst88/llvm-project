// RUN: llvm-mc -triple ia64 %s | FileCheck %s

// in0, loc0 and out0 are the registers of the frame that the last alloc or
// .regstk described, and alloc may name its own that way.

// CHECK: alloc r34 = ar.pfs, 2, 3, 2, 0
	alloc loc0 = ar.pfs, 2, 3, 2, 0
// CHECK: adds r8 = 0, r32
	mov r8 = in0
// CHECK: adds r8 = 0, r33
	mov r8 = in1
// CHECK: adds r8 = 0, r34
	mov r8 = loc0
// CHECK: adds r8 = 0, r36
	mov r8 = loc2
// CHECK: adds r8 = 0, r37
	mov r8 = out0
// CHECK: adds r8 = 0, r38
	mov r8 = out1
	;;
	.regstk 8, 0, 1, 0
// CHECK: (p6) adds r40 = 0, r39
	(p6) mov out0 = in7
// CHECK: ld8 r8 = [r33], 8
	ld8 r8 = [in1], 8
	;;

// ar.pfs may be left out, and the sizes may be expressions.
	n = 1
// CHECK: alloc r2 = ar.pfs, 1, 2, 4, 0
	alloc r2 = n, n + 1, 4, 0
// CHECK: adds r8 = 0, r38
	mov r8 = out3
	;;

// IN0 and in00 are symbols.
// CHECK: addl r8 = IN0, r0
	mov r8 = IN0
// CHECK: addl r9 = in00, r0
	mov r9 = in00
// CHECK: addl r10 = in0, r0
	mov r10 = in0#
	;;

// .rotr, .rotf and .rotp name runs of the rotating registers, each run
// beginning where the one before it ends.
// CHECK: alloc r2 = ar.pfs, 2, 6, 0, 8
	alloc r2 = ar.pfs, 2, 6, 0, 8
	.rotr a[2], b[3]
	.rotf fa[n], fb[2]
	.rotp pa[2], pb[46]
// CHECK: adds r32 = 0, r33
	mov a[0] = a[1]
// CHECK: adds r34 = 0, r36
	mov b[0] = b[n + 1]
// CHECK: ld8 r8 = [r35]
	ld8 r8 = [b[1]]
// CHECK: fmerge.s f32 = f34, f34
	mov fa[0] = fb[1]
// CHECK: (p63) cmp.eq p16, p17 = r32, r34
	(pb[45]) cmp.eq pa[0], pa[1] = in0, loc0
	;;

// A directive takes the names of its kind back, and no other.
	.rotr c[8]
// CHECK: adds r39 = 0, r8
	mov c[7] = r8
// CHECK: fmerge.s f6 = f33, f33
	mov f6 = fb[0]
// CHECK: addl r9 = a, r0
	mov r9 = a
