// RUN: llvm-mc -triple=ia64 -filetype=obj %s | llvm-objdump -d - \
// RUN:   | FileCheck %s

// Nothing may follow cover, clrrrb, rfi or bsw in an instruction group, so
// the group ends at one of them whether or not the source says so. A stop
// follows a B slot only at the end of a bundle, and so each of them has the
// last slot of its bundle.

	.text
t1:
	mov r8 = r9
	cover
	mov r10 = r11
	rfi
	bsw.0
	mov r12 = r13
	bsw.1
	clrrrb
	clrrrb.pr
t2:
	invala
	cover ;;
	clrrrb ;;
	alloc r16 = ar.pfs, 0, 2, 3, 0
	mov r8 = r9
	mov r10 = r11
	mov r12 = r13
	bsw.1
	br.ret.sptk b0

// A stop before one of them, or before a branch of a loop, which has the
// last slot to be in as well, is after the slot that the instruction left:
// in the middle of the bundle if the template has an I slot there, and at
// its end if not.
t3:
	mov cr.iip = r15 ;;
	rfi
	mov r8 = r9 ;;
	br.wtop.sptk t3
	nop 0
	mov r8 = r9 ;;
	br.ret.sptk b0


// CHECK-LABEL: <t1>:
// CHECK-NEXT:  mov r8 = r9 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  cover ;;
// CHECK-NEXT:  mov r10 = r11 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  rfi ;;
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  bsw.0 ;;
// CHECK-NEXT:  mov r12 = r13 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  bsw.1 ;;
// CHECK-NEXT:  nop.m 0 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  clrrrb ;;
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  clrrrb.pr ;;
// CHECK-LABEL: <t2>:
// CHECK-NEXT:  invala // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  cover ;;
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  clrrrb ;;
// CHECK-NEXT:  alloc r16 = ar.pfs, 0, 2, 3, 0 // [MII]
// CHECK-NEXT:  mov r8 = r9
// CHECK-NEXT:  mov r10 = r11
// CHECK-NEXT:  mov r12 = r13 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  bsw.1 ;;
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  br.ret.sptk.few b0
// CHECK-LABEL: <t3>:
// CHECK-NEXT:  mov cr.iip = r15 // [MII]
// CHECK-NEXT:  nop.i 0 ;;
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  rfi ;;
// CHECK-NEXT:  mov r8 = r9 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  nop.b 0 ;;
// CHECK-NEXT:  nop.m 0 // [MBB]
// CHECK-NEXT:  nop.b 0
// CHECK-NEXT:  br.wtop.sptk.few 0xb0
// CHECK-NEXT:  nop.m 0 // [MII]
// CHECK-NEXT:  mov r8 = r9 ;;
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  nop.m 0 // [MIB]
// CHECK-NEXT:  nop.i 0
// CHECK-NEXT:  br.ret.sptk.few b0 ;;
