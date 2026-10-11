// RUN: llvm-mc -triple=ia64 -filetype=obj %s | llvm-objdump -d -z - \
// RUN:   | FileCheck %s

// An instruction that puts a translation in, or takes one out everywhere,
// has to end its instruction group. It is given the first slot of a bundle
// and a stop after it there, whatever template the bundle would have had,
// and what is before it is left in a bundle of its own with no stop that the
// source did not ask for.

	.text
t1:
	ld8 r14 = [r15]
	itc.i r18
	mov r16 = r17
	br.ret.sptk b0 ;;
t2:
	mov r14 = r15 ;;
	itc.d r18
	ptc.g r2, r3
	ptc.ga r2, r3
	rfi
t3:
	.mii
	itc.i r18
	mov r2 = r3
	mov r4 = r5
t4:
	itc.d r18
	flushrs
	itc.d r18
	.align 32
	flushrs
	br.ret.sptk b0 ;;

// CHECK-LABEL: <t1>:
// CHECK-NEXT: ld8 r14 = [r15] // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: itc.i r18 ;; // [MMI]{{$}}
// CHECK-NEXT: mov r16 = r17{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;{{$}}
// CHECK-LABEL: <t2>:
// CHECK-NEXT: mov r14 = r15 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: itc.d r18 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ptc.g r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ptc.ga r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: rfi ;;{{$}}
// CHECK-LABEL: <t3>:
// CHECK-NEXT: itc.i r18 ;; // [MMI]{{$}}
// CHECK-NEXT: mov r2 = r3{{$}}
// CHECK-NEXT: mov r4 = r5{{$}}
// CHECK-LABEL: <t4>:
// CHECK-NEXT: itc.d r18 ;; // [MMI]{{$}}
// CHECK-NEXT: flushrs{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: itc.d r18 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: flushrs // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.few b0 ;;{{$}}
