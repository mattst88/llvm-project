// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -d -z - | FileCheck %s
// RUN: not llvm-mc -triple ia64 -filetype=obj --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR --implicit-check-not=error:

// The branches of a loop fault in any slot of a bundle but the last, so
// that is the one they have, with no-ops before them.

	.text
t0:
	br.cloop.sptk.few t0
// CHECK-LABEL: <t0>:
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.cloop.sptk.few
t1:
	.mbb
	ld8 r15 = [r14], 8
	br.ctop.sptk.few t1
	nop 0
// CHECK-LABEL: <t1>:
// CHECK-NEXT: ld8 r15 = [r14], 8 // [MBB]{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: br.ctop.sptk.few
// CHECK-NEXT: nop.m 0
t2:
	.mmb
	br.cexit.sptk.few t2
	br.cexit.sptk.few t2
// CHECK-LABEL: <t2>:
// CHECK-NEXT: nop.m 0 // [MMB]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: br.cexit.sptk.few
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.cexit.sptk.few

// One that follows another branch shares its bundle; one that another
// branch follows does not.
t3:
	br.cond.sptk.few t3
	(p6) br.wtop.sptk.few t3
	(p6) br.wexit.sptk.few t3
	br.ret.sptk.many b0
// CHECK-LABEL: <t3>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: br.cond.sptk.few
// CHECK-NEXT: (p6) br.wtop.sptk.few
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.wexit.sptk.few
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.many b0

// A template whose last slot is not for a branch has no place for one.
.ifdef ERR
	.mmi
// ERR: [[#@LINE+1]]:2: error: instruction does not fit the MMI template
	br.cloop.sptk.few t0
.endif
