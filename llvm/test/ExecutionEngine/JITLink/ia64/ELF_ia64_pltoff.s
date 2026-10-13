# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -abs ext=0x7000000 -check %s %t.o

# @pltoff is where a function descriptor to call a function by is, counted
# from the global pointer. A function of the graph has its code in it. One of
# another graph is far from the global pointer and so is its descriptor, so
# one is made here, and it enters the function by a stub.

	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	.globl	main_code
	.type	main_code,@object
main_code:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.type	local_fn,@function
	.proc	local_fn
local_fn:
	.globl	local_code
	.type	local_code,@object
local_code:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	local_fn
	.size	local_fn, .-local_fn

# jitlink-check: *{8}(section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT) + (*{8}pltoff64)) = local_code
# jitlink-check: *{8}(section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT) + (*{8}pltoff64) + 8) = section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT)
# jitlink-check: (*{8}pltoff22)[24:18] = (*{8}pltoff64)[6:0]
# jitlink-check: (*{8}pltoff22)[40:32] = (*{8}pltoff64)[15:7]
# jitlink-check: (*{8}pltoff22)[31:27] = (*{8}pltoff64)[20:16]
# jitlink-check: (*{8}pltoff22)[41:41] = (*{8}pltoff64)[21:21]
	.globl	pltoff22
	.type	pltoff22,@object
pltoff22:
	addl	r14 = @pltoff(local_fn), r1
	nop.m	0
	nop.i	0 ;;

# That of a function which other graphs see is the one they see.
# jitlink-check: section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT) + (*{8}pltoff64_global) = main
# jitlink-check: *{8}main = main_code

# jitlink-check: *{8}(section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT) + (*{8}pltoff64_ext)) = stub_addr(ELF_ia64_pltoff.s.tmp.o, ext)
# jitlink-check: *{8}(section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT) + (*{8}pltoff64_ext) + 8) = section_addr(ELF_ia64_pltoff.s.tmp.o, $__GOT)
# jitlink-check: *{8}(got_addr(ELF_ia64_pltoff.s.tmp.o, ext)) = ext

# The movl has the seven low bits of the same offset.
# jitlink-check: (*{8}(pltoff64i + 8))[42:36] = (*{8}pltoff64_ext)[6:0]
# jitlink-check: (*{8}(pltoff64i + 8))[59:59] = (*{8}pltoff64_ext)[63:63]
	.globl	pltoff64i
	.type	pltoff64i,@object
pltoff64i:
	nop.m	0
	movl	r14 = @pltoff(ext) ;;

	.data
	.globl	pltoff64
	.type	pltoff64,@object
pltoff64:
	data8	@pltoff(local_fn)
	.globl	pltoff64_global
	.type	pltoff64_global,@object
pltoff64_global:
	data8	@pltoff(main)
	.globl	pltoff64_ext
	.type	pltoff64_ext,@object
pltoff64_ext:
	data8	@pltoff(ext)
