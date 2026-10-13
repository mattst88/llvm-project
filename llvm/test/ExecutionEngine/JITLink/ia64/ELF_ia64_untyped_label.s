# RUN: split-file %s %t
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/a.o %t/a.s
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/b.o %t/b.s
# RUN: llvm-jitlink -noexec -check %t/b.s %t/a.o %t/b.o

# b.o has a function that is a global label and no more: it has no .type.
# a.o calls it and takes its address. A graph knows a name of another graph
# by one address, which for a function is that of its descriptor, so the
# label is given one as a function with a type is.

#--- a.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	.globl	a_call
	.type	a_call,@object
a_call:
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = lab ;;
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = b_fn ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.data
	.globl	a_ptr
	.type	a_ptr,@object
a_ptr:
	data8	@fptr(lab)
	.size	a_ptr, .-a_ptr

#--- b.s
	.text
	.globl	lab
lab:
	.globl	lab_code
	.type	lab_code,@object
lab_code:
	addl	r14 = @ltoff(b_var), r1 ;;
	ld8	r8 = [r14]
	br.ret.sptk.many b0 ;;

# The name is that of a descriptor: the code, and the global pointer of the
# graph the code is in.
# jitlink-check: *{8}lab = lab_code
# jitlink-check: *{8}(lab + 8) = section_addr(b.o, $__GOT)

# The call in a.o is of a stub that loads the descriptor.
# jitlink-check: (*{8}(a_call + 8))[55:36] = (stub_addr(a.o, lab) - a_call)[23:4]
# jitlink-check: *{8}(got_addr(a.o, lab)) = lab

# The address is the same one in both graphs.
# jitlink-check: *{8}a_ptr = lab
# jitlink-check: *{8}b_ptr = lab

# A call in the graph of the label is of the code, with no stub.
	.globl	b_fn
	.type	b_fn,@function
	.proc	b_fn
b_fn:
	.globl	b_call
	.type	b_call,@object
b_call:
# jitlink-check: (*{8}(b_call + 8))[55:36] = (lab_code - b_call)[23:4]
# jitlink-check: (*{8}(b_call + 8))[59:59] = (lab_code - b_call)[24:24]
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = lab ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	b_fn
	.size	b_fn, .-b_fn

	.data
	.globl	b_ptr
	.type	b_ptr,@object
b_ptr:
	data8	@fptr(lab)
	.size	b_ptr, .-b_ptr
	.globl	b_var
	.type	b_var,@object
b_var:
	data8	7
	.size	b_var, .-b_var
