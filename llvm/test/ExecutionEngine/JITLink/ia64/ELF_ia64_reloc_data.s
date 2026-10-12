# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -abs ext=0x7000000 -check %s %t.o

# Words of data. The address of a function is that of its descriptor, which is
# what the name of a function that is seen from outside stands for once it is
# linked; the code is found through the descriptor.

	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	.globl	main_code
main_code:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.type	hidden_fn,@function
	.proc	hidden_fn
hidden_fn:
	.globl	hidden_code
hidden_code:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	hidden_fn
	.size	hidden_fn, .-hidden_fn

	.data
	.globl	var
var:
	data8	0x1234

# jitlink-check: *{8}abs64 = var + 8
	.globl	abs64
abs64:
	data8	var + 8

# jitlink-check: *{4}abs32 = ext + 4
	.globl	abs32
abs32:
	data4	ext + 4
	data4	0

# jitlink-check: *{8}rel64 = var - rel64
	.globl	rel64
rel64:
	data8	@pcrel(var)

# jitlink-check: *{4}rel32 = (var - rel32) & 0xffffffff
	.globl	rel32
rel32:
	data4	@pcrel(var)
	data4	0

# The global pointer is at the linkage table.
# jitlink-check: *{8}gprel64 = var - section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)
	.globl	gprel64
gprel64:
	data8	@gprel(var)

# jitlink-check: *{4}gprel32 = (var - section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)) & 0xffffffff
	.globl	gprel32
gprel32:
	data4	@gprel(var)
	data4	0

# What is relative to the segment is relative to one place for the graph.
# jitlink-check: (*{8}segrel_a) - (*{8}segrel_b) = main_code - var
	.globl	segrel_a
segrel_a:
	data8	@segrel(main)
	.globl	segrel_b
segrel_b:
	data8	@segrel(var)

# A plain word for a function is the address of its code.
# jitlink-check: *{8}code_addr = main_code
	.globl	code_addr
code_addr:
	data8	main

# The descriptor of a function that is seen holds its code and the global
# pointer.
# jitlink-check: *{8}fptr = main
# jitlink-check: *{8}main = main_code
# jitlink-check: *{8}(main + 8) = section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)
	.globl	fptr
fptr:
	data8	@fptr(main)

# A function that is not seen gets a descriptor of its own.
# jitlink-check: *{8}(*{8}fptr_hidden) = hidden_code
# jitlink-check: *{8}((*{8}fptr_hidden) + 8) = section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)
	.globl	fptr_hidden
fptr_hidden:
	data8	@fptr(hidden_fn)

# A function of another graph is known by its descriptor already.
# jitlink-check: *{8}fptr_ext = ext
	.globl	fptr_ext
fptr_ext:
	data8	@fptr(ext)

# The linkage table entry for a descriptor, as an offset from the global
# pointer.
# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT) + *{8}ltoff_fptr) = main
	.globl	ltoff_fptr
ltoff_fptr:
	data8	@ltoff(@fptr(main))

# A descriptor in place, as a virtual table has. That of a function of the
# graph has its code, and that of another function has a stub, which enters
# the function by its own descriptor.
# jitlink-check: *{8}iplt = main_code
# jitlink-check: *{8}(iplt + 8) = section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)
	.globl	iplt
iplt:
	data16	@iplt(main)

# jitlink-check: *{8}iplt_ext = stub_addr(ELF_ia64_reloc_data.s.tmp.o, ext)
# jitlink-check: *{8}(iplt_ext + 8) = section_addr(ELF_ia64_reloc_data.s.tmp.o, $__GOT)
# jitlink-check: *{8}(got_addr(ELF_ia64_reloc_data.s.tmp.o, ext)) = ext
	.globl	iplt_ext
iplt_ext:
	data16	@iplt(ext)
