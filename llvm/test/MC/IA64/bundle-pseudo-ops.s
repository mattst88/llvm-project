// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -d - | FileCheck %s

// The pseudo-ops that leave the unit to the bundle. The bundles are those of
// GNU as, which this file was checked against.

	.text

// A pseudo-op that several units can execute is the instruction of the unit
// whose slot it gets, by the rule that places any other instruction.
t0:
	nop 0
// CHECK-LABEL: <t0>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t1:
	nop 1
	nop 2
	nop 3
// CHECK-LABEL: <t1>:
// CHECK-NEXT: nop.m 1 // [MII]{{$}}
// CHECK-NEXT: nop.i 2{{$}}
// CHECK-NEXT: nop.i 3{{$}}
t2:
	nop 0
	fadd f6 = f7, f8
// CHECK-LABEL: <t2>:
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f6 = f7, f1, f8{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t3:
	nop 1
	nop 2
	fadd f6 = f7, f8
// CHECK-LABEL: <t3>:
// CHECK-NEXT: nop.m 1 // [MMF]{{$}}
// CHECK-NEXT: nop.m 2{{$}}
// CHECK-NEXT: fma f6 = f7, f1, f8{{$}}
t4:
	fadd f6 = f7, f8
	nop 0
// CHECK-LABEL: <t4>:
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f6 = f7, f1, f8{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t5:
	br.ret.sptk.many b0
	nop 0
// CHECK-LABEL: <t5>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
t6:
	br.ret.sptk.many b0
	break 1
	nop 2
// CHECK-LABEL: <t6>:
// CHECK-NEXT: br.ret.sptk.many b0 // [BBB]{{$}}
// CHECK-NEXT: break.b 1{{$}}
// CHECK-NEXT: nop.b 2{{$}}
t7:
	add r4 = r5, r6
	hint 1
	break 2
// CHECK-LABEL: <t7>:
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: hint.i 1{{$}}
// CHECK-NEXT: break.i 2{{$}}
t8:
	(p6) nop 0
	movl r4 = 5
// CHECK-LABEL: <t8>:
// CHECK-NEXT: (p6) nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r4 = 5{{$}}
t9:
	ld8 r4 = [r5]
	nop 0 ;;
	nop 1
// CHECK-LABEL: <t9>:
// CHECK-NEXT: ld8 r4 = [r5] // [MII]{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: nop.i 1{{$}}

// chk.s is an M-unit or an I-unit instruction.
t10:
	chk.s r4, t0
// CHECK-LABEL: <t10>:
// CHECK-NEXT: chk.s.m r4, 0x0 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t11:
	ld8 r4 = [r5]
	chk.s r4, t0
// CHECK-LABEL: <t11>:
// CHECK-NEXT: ld8 r4 = [r5] // [MII]{{$}}
// CHECK-NEXT: chk.s.i r4, 0x0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t12:
	ld8 r4 = [r5]
	ld8 r6 = [r7]
	chk.s r4, t0
// CHECK-LABEL: <t12>:
// CHECK-NEXT: ld8 r4 = [r5] // [MMI]{{$}}
// CHECK-NEXT: ld8 r6 = [r7]{{$}}
// CHECK-NEXT: chk.s.i r4, 0x0{{$}}
t13:
	shr r4 = r5, 3
	chk.s r4, t0
// CHECK-LABEL: <t13>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: shr r4 = r5, 3{{$}}
// CHECK-NEXT: chk.s.i r4, 0x0{{$}}

// A move to or from an application register goes to the unit that has the
// register: M for ar0 to ar47 and I for ar64 to ar111. Either has the rest.
t14:
	mov ar.pfs = r4
// CHECK-LABEL: <t14>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: mov.i ar.pfs = r4{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t15:
	mov ar.lc = 5
// CHECK-LABEL: <t15>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: mov.i ar.lc = 5{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t16:
	mov r4 = ar.unat
	mov r5 = ar.pfs
// CHECK-LABEL: <t16>:
// CHECK-NEXT: mov.m r4 = ar.unat // [MII]{{$}}
// CHECK-NEXT: mov.i r5 = ar.pfs{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t17:
	nop 0
	mov r4 = ar.unat
// CHECK-LABEL: <t17>:
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: mov.m r4 = ar.unat{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t18:
	mov ar48 = r4
	mov ar49 = r4
// CHECK-LABEL: <t18>:
// CHECK-NEXT: mov.m ar48 = r4 // [MII]{{$}}
// CHECK-NEXT: mov.i ar49 = r4{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t19:
	mov r4 = ar112
	mov r5 = ar127
	mov ar.ccv = r4
// CHECK-LABEL: <t19>:
// CHECK-NEXT: mov.m r4 = ar112 // [MII]{{$}}
// CHECK-NEXT: mov.i r5 = ar127{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: mov.m ar.ccv = r4 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// "hint" and "chk.s" have no B-unit form, but GNU as picks the template as
// though they had, and ends the bundle when one gets a B slot. The hint is
// then hint.i.
t20:
	br.ret.sptk.many b0
	hint 1
// CHECK-LABEL: <t20>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: hint.i 1{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t21:
	br.ret.sptk.many b0
	hint 1
	ld8 r4 = [r5]
// CHECK-LABEL: <t21>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: hint.i 1{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r4 = [r5] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t22:
	br.ret.sptk.many b0
	chk.s r4, t0
	nop 0
// CHECK-LABEL: <t22>:
// CHECK-NEXT: br.ret.sptk.many b0 // [BBB]{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: chk.s.m r4, 0x0 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// The hint of the M unit has two bits fewer for its number, so one with a
// number that needs them is not an M-unit instruction. GNU as says of this
// one that the number is out of range, as it tries the M slot first.
t23:
	hint 0x80000
	hint 5
// CHECK-LABEL: <t23>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: hint.i 524288{{$}}
// CHECK-NEXT: hint.i 5{{$}}
t24:
	hint 1
	br.ret.sptk.many b0
// CHECK-LABEL: <t24>:
// CHECK-NEXT: hint.m 1 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.many b0 ;;{{$}}
