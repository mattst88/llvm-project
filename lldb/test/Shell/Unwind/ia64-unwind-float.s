# REQUIRES: ia64-registered-target, lld

# An IA-64 floating-point register is saved as 16 bytes, which is not the
# format of a double. lldb has such a register twice: fN_raw is the bytes,
# and fN their value as a double, which is what a variable that lives in the
# register is read from. Check that both are found for a caller whose f2 and
# f3 a function saved on the memory stack, that the value is worked out of
# the bytes, and that a register no one saved is the one in the note.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: yaml2obj %S/Inputs/ia64-unwind-float.yaml -o %t.core
# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "thread backtrace" \
# RUN:   -o "register read f2 f3 f16 f6 f2_raw" \
# RUN:   -o "frame select 1" -o "register read f2 f3 f16 f6 f2_raw" \
# RUN:   -o "frame select 2" \
# RUN:   -o "register read f2 f3 f16 f6 f2_raw f3_raw f16_raw" 2>&1 \
# RUN:   | FileCheck %s

# CHECK: frame #0: 0x4000000000000200 {{.*}}`leaf
# CHECK-NEXT: frame #1: 0x40000000000001f0 {{.*}}`keeps + 48
# CHECK-NEXT: frame #2: 0x40000000000001a0 {{.*}}`_start + 16
# CHECK-NOT: frame #3

# CHECK: register read
# CHECK-NEXT: f2 = 1.5
# CHECK-NEXT: f3 = 0.5
# CHECK-NEXT: f16 = 2
# CHECK-NEXT: f6 = 0
# CHECK-NEXT: f2_raw = 0x000000000000ffffc000000000000000

## leaf saves nothing, so its caller's are what the note has, but for f6,
## which a call does not preserve.
# CHECK: frame select 1
# CHECK: register read
# CHECK-NEXT: f2 = 1.5
# CHECK-NEXT: f3 = 0.5
# CHECK-NEXT: f16 = 2
# CHECK-NEXT: f6 = error: unavailable
# CHECK-NEXT: f2_raw = 0x000000000000ffffc000000000000000

## f3 was saved with an integer in it, which is not normalized.
# CHECK: frame select 2
# CHECK: register read
# CHECK-NEXT: f2 = 3.25
# CHECK-NEXT: f3 = -42
# CHECK-NEXT: f16 = 2
# CHECK-NEXT: f6 = error: unavailable
# CHECK-NEXT: f2_raw = 0x0000000000010000d000000000000000
# CHECK-NEXT: f3_raw = 0x000000000003003e000000000000002a
# CHECK-NEXT: f16_raw = 0x00000000000100008000000000000000

	.text
	.globl	_start
	.proc	_start
_start:
	.prologue
	.save	ar.pfs, r32
	alloc	r32 = ar.pfs, 0, 2, 0, 0
	.save	rp, r33
	mov	r33 = b0
	.body
	br.call.sptk.many b0 = keeps ;;
	mov	b0 = r33
	mov	ar.pfs = r32
	br.ret.sptk.many b0 ;;
	.endp	_start

	.globl	keeps
	.proc	keeps
keeps:
	.prologue
	.save	ar.pfs, r32
	alloc	r32 = ar.pfs, 0, 2, 0, 0
	adds	r16 = -16, r12
	.fframe	32
	adds	r12 = -32, r12 ;;
	.save.f	0x1
	stf.spill [r16] = f2, 16
	.save	rp, r33
	mov	r33 = b0 ;;
	.save.f	0x2
	stf.spill [r16] = f3
	.body
	br.call.sptk.many b0 = leaf ;;
	break	0 ;;
	.endp	keeps

	.globl	leaf
	.proc	leaf
leaf:
	.prologue
	.body
	ld8	r8 = [r0]
	br.ret.sptk.many b0 ;;
	.endp	leaf
