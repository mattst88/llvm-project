// RUN: llvm-mc -triple=ia64 -filetype=obj %s | llvm-objdump -d -z - \
// RUN:   | FileCheck %s

// Nothing may precede alloc, flushrs or loadrs in an instruction group, so
// the group before one of them ends whether or not the source says so: at the
// instruction before it, or with the bundle before it, or with each bundle of
// the padding that an alignment put before it. Other padding has no stops,
// and neither has the padding before a procedure.

	.text
t1:
	mov r33 = 0
	flushrs
	mov r34 = 0
	br.ret.sptk b0 ;;
	.align 64
t2:
	alloc r32 = ar.pfs, 0, 3, 4, 0
	mov r2 = r3
	loadrs
	mov r3 = r4
	mov r5 = r6
t3:
	alloc r32 = ar.pfs, 0, 3, 4, 0
	br.ret.sptk b0 ;;
	.align 64
t4:
	mov r2 = r3
	br.ret.sptk b0 ;;
	.align 32
	.align 64
t5:
	loadrs
	br.ret.sptk b0 ;;
	.align 32
	.proc t6
t6:
	alloc r32 = ar.pfs, 0, 3, 4, 0
	br.ret.sptk b0 ;;
	.endp t6

// CHECK-LABEL: <t1>:
// CHECK-NEXT: mov r33 = 0 ;; // [MMI]{{$}}
// CHECK-NEXT: flushrs{{$}}
// CHECK-NEXT: mov r34 = 0{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-LABEL: <t2>:
// CHECK-NEXT: alloc r32 = ar.pfs, 0, 3, 4, 0 // [MII]{{$}}
// CHECK-NEXT: mov r2 = r3{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-NEXT: loadrs // [MII]{{$}}
// CHECK-NEXT: mov r3 = r4{{$}}
// CHECK-NEXT: mov r5 = r6 ;;
// CHECK-LABEL: <t3>:
// CHECK-NEXT: alloc r32 = ar.pfs, 0, 3, 4, 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-LABEL: <t4>:
// CHECK-NEXT: mov r2 = r3 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-LABEL: <t5>:
// CHECK-NEXT: loadrs // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-LABEL: <t6>:
// CHECK-NEXT: alloc r32 = ar.pfs, 0, 3, 4, 0 // [MIB]{{$}}
