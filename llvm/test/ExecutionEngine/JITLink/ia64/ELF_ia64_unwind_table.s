# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -check %s %t.o

# Each section of functions has an unwind table of its own, and nothing
# refers to one. The tables are kept with the code they are for and put in
# one section, and their entries, which are a start, an end and where the
# unwind information is, each counted from the start of the segment, are put
# in the order of the functions for an unwinder to search.

# The first entry is for main and the second for second. The table of the
# function that nothing uses went with it: its entry would be the first.
# jitlink-check: (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 24)) - (*{8}section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind)) = second_code - main_code
# jitlink-check: (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 8)) - (*{8}section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind)) = 16
# jitlink-check: (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 32)) - (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 24)) = 32
# jitlink-check: (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 40)) - (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 16)) = second_info - main_info

# The information is as far from the segment as the entry says.
# jitlink-check: (*{8}(section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind) + 16)) - (*{8}section_addr(ELF_ia64_unwind_table.s.tmp.o, .IA_64.unwind)) = main_info - main_code

	.section	.text.unused,"ax",@progbits
	.type	unused,@function
	.proc	unused
unused:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	unused
	.size	unused, .-unused
1:

	.section	.text.main,"ax",@progbits
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
2:

	.section	.text.second,"ax",@progbits
	.globl	second
	.type	second,@function
	.proc	second
second:
	.globl	second_code
second_code:
	nop.m	0
	nop.m	0
	nop.i	0 ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	second
	.size	second, .-second
3:

# The tables, the last function first.
	.section	.IA_64.unwind.text.second,"ao",@unwind,.text.second
	data8	@segrel(second)
	data8	@segrel(3b)
	data8	@segrel(second_info)

	.section	.IA_64.unwind.text.unused,"ao",@unwind,.text.unused
	data8	@segrel(unused)
	data8	@segrel(1b)
	data8	@segrel(unused_info)

	.section	.IA_64.unwind.text.main,"ao",@unwind,.text.main
	data8	@segrel(main)
	data8	@segrel(2b)
	data8	@segrel(main_info)

	.section	.IA_64.unwind_info.text.second,"a",@progbits
	.globl	second_info
second_info:
	data8	0x1000000000000001
	data8	0

	.section	.IA_64.unwind_info.text.unused,"a",@progbits
unused_info:
	data8	0x1000000000000001
	data8	0

	.section	.IA_64.unwind_info.text.main,"a",@progbits
	.globl	main_info
main_info:
	data8	0x1000000000000001
	data8	0
