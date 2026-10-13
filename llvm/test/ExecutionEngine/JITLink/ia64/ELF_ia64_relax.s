# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -abs ext=0x7000000 -check %s %t.o
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj --defsym BAD=1 \
# RUN:   -o %t.bad.o %s
# RUN: not llvm-jitlink -noexec -abs ext=0x7000000 %t.bad.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=BAD

# An addl that @ltoffx marks takes the address of a linkage table entry, and
# only loads that ld8.mov marks use it. Where what the entry holds is within
# reach of the global pointer the addl takes the address itself and the load
# becomes a move, or nothing where it moved a register to itself.

# BAD: LinkageTableLoad at {{.*}} is not at a load of eight bytes

	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

# jitlink-check: (*{8}near)[24:18] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[6:0]
# jitlink-check: (*{8}near)[40:32] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[15:7]
# jitlink-check: (*{8}near)[31:27] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[20:16]
# jitlink-check: (*{8}near)[41:41] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[21:21]
# jitlink-check: *{8}near_load = *{8}nothing
# jitlink-check: *{8}(near_load + 8) = *{8}(nothing + 8)
	.globl	near
	.type	near,@object
near:
	addl	r14 = @ltoffx(var), r1
	nop.m	0
	nop.i	0 ;;
	.globl	near_load
	.type	near_load,@object
near_load:
	ld8.mov	r14 = [r14], var
	nop.m	0
	nop.i	0 ;;

# The move keeps the predicate of the load.
# jitlink-check: (*{8}other)[24:18] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[6:0]
# jitlink-check: (*{8}other)[40:32] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[15:7]
# jitlink-check: (*{8}other)[31:27] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[20:16]
# jitlink-check: (*{8}other)[41:41] = (var - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[21:21]
# jitlink-check: *{8}other_load = *{8}move
# jitlink-check: *{8}(other_load + 8) = *{8}(move + 8)
	.globl	other
	.type	other,@object
other:
	addl	r14 = @ltoffx(var), r1
	nop.m	0
	nop.i	0 ;;
	.globl	other_load
	.type	other_load,@object
other_load:
	(p6) ld8.mov	r15 = [r14], var
	nop.m	0
	nop.i	0 ;;

# A place in something is as near as it is, whatever the thing.
# jitlink-check: (*{8}addend)[24:18] = (var + 8 - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[6:0]
# jitlink-check: (*{8}addend)[40:32] = (var + 8 - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[15:7]
# jitlink-check: (*{8}addend)[31:27] = (var + 8 - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[20:16]
# jitlink-check: (*{8}addend)[41:41] = (var + 8 - section_addr(ELF_ia64_relax.s.tmp.o, $__GOT))[21:21]
# jitlink-check: *{8}addend_load = *{8}nothing
# jitlink-check: *{8}(addend_load + 8) = *{8}(nothing + 8)
	.globl	addend
	.type	addend,@object
addend:
	addl	r14 = @ltoffx(var + 8), r1
	nop.m	0
	nop.i	0 ;;
	.globl	addend_load
	.type	addend_load,@object
addend_load:
	ld8.mov	r14 = [r14], var + 8
	nop.m	0
	nop.i	0 ;;

# What is far keeps its entry and its load.
# jitlink-check: *{8}(section_addr(ELF_ia64_relax.s.tmp.o, $__GOT) + (*{8}far)[24:18]) = ext
# jitlink-check: (*{8}far)[41:41] = 0
# jitlink-check: *{8}far_load = *{8}load
# jitlink-check: *{8}(far_load + 8) = *{8}(load + 8)
	.globl	far
	.type	far,@object
far:
	addl	r14 = @ltoffx(ext), r1
	nop.m	0
	nop.i	0 ;;
	.globl	far_load
	.type	far_load,@object
far_load:
	(p6) ld8.mov	r15 = [r14], ext
	nop.m	0
	nop.i	0 ;;

# What the relaxed and the kept loads are to be.
	.globl	nothing
	.type	nothing,@object
nothing:
	nop.m	0
	nop.m	0
	nop.i	0 ;;
	.globl	move
	.type	move,@object
move:
	(p6) adds	r15 = 0, r14
	nop.m	0
	nop.i	0 ;;
	.globl	load
	.type	load,@object
load:
	(p6) ld8	r15 = [r14]
	nop.m	0
	nop.i	0 ;;

.ifdef BAD
	addl	r14 = @ltoffx(var), r1
	nop.m	0
	nop.i	0 ;;
1:
	ld4	r14 = [r14]
	nop.m	0
	nop.i	0 ;;
	.reloc	1b, R_IA64_LDXMOV, var
.endif

	.data
	.globl	var
	.type	var,@object
var:
	data8	0x1234
	data8	0x5678
