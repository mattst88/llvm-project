# REQUIRES: ia64-registered-target, lld

# An IA-64 function that keeps what it saves in stacked registers needs
# nothing on the memory stack, so any number of frames in a row can have the
# same CFA. They are not an unwind that loops: each has a frame of its own in
# the register backing store, and so an alternate frame address of its own.
# Check that the backtrace goes through them.
#
# leaf has no entry in the unwind table, as the ABI lets a function that
# saves nothing have none, and the thread is past its first instruction.
# Check that the registers a function preserves are known in the frames
# above it, and that others are not. Those are r4-r7, the thread pointer,
# b1-b5, ar.lc, ar.unat, and the parts of the predicates and of ar.fpsr that
# make each of the two as a whole worth showing. third has an entry in the
# table, which says nothing of them, so its caller has them as well.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: yaml2obj %S/Inputs/ia64-frameless-callers.yaml -o %t.core
# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "thread backtrace" \
# RUN:   -o "frame select 1" -o "register read r4 b1 r8" \
# RUN:   -o "register read r13 lc unat pr fpsr" \
# RUN:   -o "frame select 2" -o "register read r4 b1 r8" \
# RUN:   -o "register read r13 lc unat pr fpsr" \
# RUN:   -o "frame select 3" -o "register read r12 bsp" \
# RUN:   -o "frame select 4" -o "register read r12 bsp" 2>&1 \
# RUN:   | FileCheck %s

# CHECK: frame #0: 0x40000000000002a1 {{.*}}`leaf + 1
# CHECK-NEXT: frame #1: 0x4000000000000280 {{.*}}`third + 16
# CHECK-NEXT: frame #2: 0x4000000000000250 {{.*}}`second + 16
# CHECK-NEXT: frame #3: 0x4000000000000220 {{.*}}`first + 16
# CHECK-NEXT: frame #4: 0x40000000000001f0 {{.*}}`_start + 16
# CHECK-NOT: frame #5

# CHECK: frame select 1
# CHECK: r4 = 0x4444444444444444
# CHECK-NEXT: b1 = 0x4000000000000b10
# CHECK-NEXT: r8 = error: unavailable
# CHECK: r13 = 0x2000000000900000
# CHECK-NEXT: lc = 0x0000000000000777
# CHECK-NEXT: unat = 0x0000000000000030
# CHECK-NEXT: pr = 0x0000000000010041
# CHECK-NEXT: fpsr = 0x0009804c0270033f

# CHECK: frame select 2
# CHECK: r4 = 0x4444444444444444
# CHECK-NEXT: b1 = 0x4000000000000b10
# CHECK-NEXT: r8 = error: unavailable
# CHECK: r13 = 0x2000000000900000
# CHECK-NEXT: lc = 0x0000000000000777
# CHECK-NEXT: unat = 0x0000000000000030
# CHECK-NEXT: pr = 0x0000000000010041
# CHECK-NEXT: fpsr = 0x0009804c0270033f

# CHECK: frame select 3
# CHECK: r12 = 0x200000000080f400
# CHECK-NEXT: bsp = 0x2000000000800110
# CHECK: frame select 4
# CHECK: r12 = 0x200000000080f400
# CHECK-NEXT: bsp = 0x2000000000800100

	.text
	.macro	calls name, callee
	.globl	\name
	.proc	\name
\name:
	.prologue
	.save	ar.pfs, r32
	alloc	r32 = ar.pfs, 0, 2, 0, 0
	.save	rp, r33
	mov	r33 = b0
	.body
	br.call.sptk.many b0 = \callee ;;
	mov	b0 = r33
	mov	ar.pfs = r32
	br.ret.sptk.many b0 ;;
	.endp	\name
	.endm

	calls	_start, first
	calls	first, second
	calls	second, third
	calls	third, leaf

	.globl	leaf
	.type	leaf,@function
leaf:
	ld8	r8 = [r0]
	br.ret.sptk.many b0 ;;
	.size	leaf, .-leaf
