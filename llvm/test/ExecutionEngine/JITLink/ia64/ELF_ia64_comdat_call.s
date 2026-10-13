# RUN: split-file %s %t
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/a.o %t/a.s
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/b.o %t/b.s
# RUN: llvm-jitlink -noexec -check %t/b.s %t/a.o %t/b.o

# Two objects have the COMDAT function inl, and the one in a.o is kept. Each
# graph has a global pointer of its own, and code that calls a function its
# own object defines does not restore the global pointer after the call. So
# the call in b.o is still of the inl in b.o, with no stub, and the address
# that b.o takes of inl is the address of the one kept.

#--- a.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = inl ;;
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = b_fn ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.section .text.inl,"axG",@progbits,inl,comdat
	.weak	inl
	.type	inl,@function
	.proc	inl
inl:
	.globl	a_inl
	.type	a_inl,@object
a_inl:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	inl
	.size	inl, .-inl

#--- b.s
	.text
	.globl	b_fn
	.type	b_fn,@function
	.proc	b_fn
b_fn:
	.globl	b_call
	.type	b_call,@object
b_call:
# jitlink-check: (*{8}(b_call + 8))[55:36] = (b_inl - b_call)[23:4]
# jitlink-check: (*{8}(b_call + 8))[59:59] = (b_inl - b_call)[24:24]
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = inl ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	b_fn
	.size	b_fn, .-b_fn

	.section .text.inl,"axG",@progbits,inl,comdat
	.weak	inl
	.type	inl,@function
	.proc	inl
inl:
	.globl	b_inl
	.type	b_inl,@object
b_inl:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	inl
	.size	inl, .-inl

# The address taken is the descriptor of the function kept, whose code is that
# of a.o.
# jitlink-check: *{8}b_ptr = inl
# jitlink-check: *{8}inl = a_inl
	.data
	.globl	b_ptr
	.type	b_ptr,@object
b_ptr:
	data8	@fptr(inl)
	.size	b_ptr, .-b_ptr
