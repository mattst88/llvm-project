# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -abs ext=0x7000000 -abs small=0x12345 \
# RUN:   -abs tiny=0x123 -check %s %t.o

# Immediates of instructions. An instruction is forty-one bits of a bundle of
# sixteen bytes: the first is at bit 5 of the bundle, the second at bit 46 and
# the third at bit 87, which is bit 23 of the second word. The checks below
# take the fields of an immediate out of the word they are in.

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

# The immediate of an adds: seven bits at bit 13 of the instruction, six at
# bit 27 and the sign at bit 36.
# jitlink-check: (*{8}imm14)[24:18] = tiny[6:0]
# jitlink-check: (*{8}imm14)[37:32] = tiny[12:7]
# jitlink-check: (*{8}imm14)[41:41] = 0
	.globl	imm14
imm14:
	adds	r14 = tiny, r15
	nop.m	0
	nop.i	0 ;;

# The immediate of an addl: seven bits at bit 13, five at bit 22, nine at
# bit 27 and the sign at bit 36.
# jitlink-check: (*{8}imm22)[24:18] = small[6:0]
# jitlink-check: (*{8}imm22)[40:32] = small[15:7]
# jitlink-check: (*{8}imm22)[31:27] = small[20:16]
# jitlink-check: (*{8}imm22)[41:41] = 0
	.globl	imm22
imm22:
	addl	r14 = small, r0
	nop.m	0
	nop.i	0 ;;

# The immediate of a movl. Bits 22 to 62 are the second slot, and the rest
# are in the third as those of an addl are, with bit 21 at bit 21 and bit 63
# at bit 36.
# jitlink-check: (*{8}imm64)[63:46] = var[39:22]
# jitlink-check: (*{8}(imm64 + 8))[22:0] = var[62:40]
# jitlink-check: (*{8}(imm64 + 8))[42:36] = var[6:0]
# jitlink-check: (*{8}(imm64 + 8))[58:50] = var[15:7]
# jitlink-check: (*{8}(imm64 + 8))[49:45] = var[20:16]
# jitlink-check: (*{8}(imm64 + 8))[44:44] = var[21:21]
# jitlink-check: (*{8}(imm64 + 8))[59:59] = var[63:63]
	.globl	imm64
imm64:
	nop.m	0
	movl	r14 = var ;;

# What is addressed from the global pointer, which is at the linkage table.
# jitlink-check: (*{8}gprel22)[24:18] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[6:0]
# jitlink-check: (*{8}gprel22)[40:32] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[15:7]
# jitlink-check: (*{8}gprel22)[31:27] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[20:16]
# jitlink-check: (*{8}gprel22)[41:41] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[21:21]
	.globl	gprel22
gprel22:
	addl	r14 = @gprel(far), r1
	nop.m	0
	nop.i	0 ;;

# jitlink-check: (*{8}gprel64)[63:46] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[39:22]
# jitlink-check: (*{8}(gprel64 + 8))[22:0] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[62:40]
# jitlink-check: (*{8}(gprel64 + 8))[42:36] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[6:0]
# jitlink-check: (*{8}(gprel64 + 8))[58:50] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[15:7]
# jitlink-check: (*{8}(gprel64 + 8))[49:45] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[20:16]
# jitlink-check: (*{8}(gprel64 + 8))[44:44] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[21:21]
# jitlink-check: (*{8}(gprel64 + 8))[59:59] = (far - section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT))[63:63]
	.globl	gprel64
gprel64:
	nop.m	0
	movl	r14 = @gprel(far) ;;

# A linkage table entry is found from the global pointer, and holds the
# address. The table is short, so the seven low bits are the offset.
# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT) + (*{8}ltoff22)[24:18]) = var
	.globl	ltoff22
ltoff22:
	addl	r14 = @ltoff(var), r1
	nop.m	0
	nop.i	0 ;;

# The load that follows an entry marked as one a linker may do without is
# left a load, and the entry is made.
# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT) + (*{8}ltoff22x)[24:18]) = var
# jitlink-check: *{8}(ltoff22x + 16) = *{8}ldxmov
# jitlink-check: *{8}(ltoff22x + 24) = *{8}(ldxmov + 8)
	.globl	ltoff22x
ltoff22x:
	addl	r14 = @ltoffx(var), r1
	nop.m	0
	nop.i	0 ;;
	ld8.mov	r14 = [r14], var
	nop.m	0
	nop.i	0 ;;
	.globl	ldxmov
ldxmov:
	ld8	r14 = [r14]
	nop.m	0
	nop.i	0 ;;

# An entry for a place in something is an entry of its own.
# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT) + (*{8}ltoff22_addend)[24:18]) = var + 8
	.globl	ltoff22_addend
ltoff22_addend:
	addl	r14 = @ltoff(var + 8), r1
	nop.m	0
	nop.i	0 ;;

# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT) + (*{8}(ltoff64 + 8))[42:36]) = var
	.globl	ltoff64
ltoff64:
	nop.m	0
	movl	r14 = @ltoff(var) ;;

# The entry for the address of a function holds its descriptor.
# jitlink-check: *{8}(section_addr(ELF_ia64_reloc_insn.s.tmp.o, $__GOT) + (*{8}ltoff_fptr)[24:18]) = main
# jitlink-check: *{8}main = main_code
	.globl	ltoff_fptr
ltoff_fptr:
	addl	r14 = @ltoff(@fptr(main)), r1
	nop.m	0
	nop.i	0 ;;

# A branch counts bundles from its own: twenty bits at bit 13 of the
# instruction and the sign at bit 36. A function of the graph is entered at
# its code. This one is behind the branch.
# jitlink-check: (*{8}(call + 8))[55:36] = (main_code - call)[23:4]
# jitlink-check: (*{8}(call + 8))[59:59] = 1
	.globl	call
call:
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = main ;;

# A check has seven of the bits at bit 6 and thirteen at bit 20.
# jitlink-check: (*{8}check)[17:11] = (recover - check)[10:4]
# jitlink-check: (*{8}check)[37:25] = (recover - check)[23:11]
# jitlink-check: (*{8}check)[41:41] = 0
	.globl	check
check:
	chk.s	r14, recover
	nop.m	0
	nop.i	0 ;;

# A long branch has bits 20 to 58 of the count at bit 2 of the second slot
# and the rest in the third, as a branch has them.
# jitlink-check: (*{8}(long + 8))[55:36] = (recover - long)[23:4]
# jitlink-check: (*{8}long)[63:48] = (recover - long)[39:24]
# jitlink-check: (*{8}(long + 8))[22:0] = (recover - long)[62:40]
# jitlink-check: (*{8}(long + 8))[59:59] = 0
	.globl	long
long:
	nop.m	0
	brl.sptk.many recover ;;

	.globl	recover
recover:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;

	.data
	.globl	var
var:
	data8	0x1234
	.space	0x1000
	.globl	far
far:
	data8	0x5678
