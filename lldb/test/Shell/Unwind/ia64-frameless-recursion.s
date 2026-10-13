# REQUIRES: ia64-registered-target, lld

# An IA-64 function that keeps what it saves in stacked registers and calls
# itself has frames with the same CFA that are of the same function. They are
# not an unwind that goes nowhere: each has a frame of its own in the register
# backing store, and so an alternate frame address of its own. Check that the
# backtrace goes through them.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: yaml2obj %S/Inputs/ia64-frameless-recursion.yaml -o %t.core
# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "thread backtrace" 2>&1 | FileCheck %s

# CHECK: frame #0: 0x4000000000000220 {{.*}}`leaf
# CHECK-NEXT: frame #1: 0x40000000000001f0 {{.*}}`again + 48
# CHECK-NEXT: frame #2: 0x4000000000000200 {{.*}}`again + 64
# CHECK-NEXT: frame #3: 0x4000000000000200 {{.*}}`again + 64
# CHECK-NEXT: frame #4: 0x40000000000001a0 {{.*}}`_start + 32
# CHECK-NOT: frame #5

	.text
	.globl	_start
	.proc	_start
_start:
	.prologue
	.save	ar.pfs, r32
	alloc	r32 = ar.pfs, 0, 2, 1, 0
	.save	rp, r33
	mov	r33 = b0
	.body
	mov	r34 = 2
	br.call.sptk.many b0 = again ;;
	mov	b0 = r33
	mov	ar.pfs = r32
	br.ret.sptk.many b0 ;;
	.endp	_start

	.globl	again
	.proc	again
again:
	.prologue
	.save	ar.pfs, r33
	alloc	r33 = ar.pfs, 1, 2, 1, 0
	.save	rp, r34
	mov	r34 = b0
	.body
	cmp.eq	p6, p7 = r0, r32
	adds	r35 = -1, r32 ;;
	(p6) br.call.sptk.many b0 = leaf ;;
	(p7) br.call.sptk.many b0 = again ;;
	mov	b0 = r34
	mov	ar.pfs = r33
	br.ret.sptk.many b0 ;;
	.endp	again

	.globl	leaf
	.type	leaf,@function
leaf:
	ld8	r8 = [r0]
	br.ret.sptk.many b0 ;;
	.size	leaf, .-leaf
