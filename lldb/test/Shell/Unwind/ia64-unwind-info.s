# REQUIRES: ia64-registered-target, lld

# An IA-64 binary says how to unwind in .IA_64.unwind and .IA_64.unwind_info,
# and has no .eh_frame. Check that lldb makes unwind plans out of them, and
# walks the stack of a core file with them.
#
# A frame is in two places. middle has 32 bytes of the memory stack and
# saves rp there; _start has none, and saves it in a stacked register, which
# is in the register backing store. The registers that a function has there
# are the last ones of its caller and then its own, so middle's r32 and r33
# are _start's r35 and r36.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: yaml2obj %S/Inputs/ia64-unwind-info.yaml -o %t.core

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n middle" \
# RUN:   | FileCheck %s --check-prefix=PLAN

# The frame of middle is the caller's until the second instruction has made
# its own, and the caller's again after the last but one has given it up.
# ar.pfs is in r34 after the first instruction. rp is in b0 until the fifth
# instruction, the second of the second bundle, has stored it; once sp is
# restored, what was saved in the frame is taken to be back in its register.
#
# The alternate frame address is the caller's bsp, which is the bsp of the
# frame less the registers that the caller has to itself; ar.pfs has their
# number. bsp is register 351, ar.pfs 398 and r34 34.

# PLAN: Synchronous (restricted to call-sites) UnwindPlan is 'IA-64 unwind info'
# PLAN: object file UnwindPlan:
# PLAN-NEXT: This UnwindPlan originally sourced from IA-64 unwind info
# PLAN-NEXT: This UnwindPlan is sourced from the compiler: yes.
# PLAN-NEXT: This UnwindPlan is valid at all instruction locations: yes.
# PLAN: row[0]: 0: CFA=r12 +0 AFA=DW_OP_bregx 0x18e +0, DW_OP_lit7, DW_OP_shr,
# PLAN-SAME: DW_OP_bregx 0x15f +0, DW_OP_swap, DW_OP_minus =>
# PLAN-SAME: r12=CFA+0 ip=b0 cfm=DW_OP_bregx 0x18e +0, DW_OP_constu 0x3fffffffff, DW_OP_and bsp=AFA+0
# PLAN: row[1]: 1: CFA=r12 +0 AFA=DW_OP_bregx 0x22 +0,
# PLAN-SAME: r12=CFA+0 ip=b0 cfm=DW_OP_bregx 0x22 +0, DW_OP_constu 0x3fffffffff, DW_OP_and bsp=AFA+0 pfs=r34
# PLAN: row[2]: 2: CFA=r12+32 AFA=DW_OP_bregx 0x22 +0,
# PLAN-SAME: r12=CFA+0 ip=b0 cfm=
# PLAN: row[3]: 18: CFA=r12+32 AFA=DW_OP_bregx 0x22 +0,
# PLAN-SAME: r12=CFA+0 ip=[CFA-16] cfm={{.*}} bsp=AFA+0 pfs=r34
# PLAN: row[4]: 80: CFA=r12 +0 AFA=DW_OP_bregx 0x22 +0,
# PLAN-SAME: r12=CFA+0 ip=b0 cfm={{.*}} bsp=AFA+0 pfs=r34

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "thread backtrace" \
# RUN:   -o "frame select 1" -o "register read ip r12 bsp cfm" \
# RUN:   -o "register read r32 r33 r34 r35 r36 r37 r38" \
# RUN:   -o "frame select 2" -o "register read ip r12 bsp cfm" \
# RUN:   -o "register read r32 r33 r34 r35 r36 r37" 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CORE

# CORE: frame #0: 0x4000000000000221 {{.*}}`leaf + 1
# CORE-NEXT: frame #1: 0x40000000000001f0 {{.*}}`middle + 48
# CORE-NEXT: frame #2: 0x40000000000001a0 {{.*}}`_start + 32
# CORE-NOT: frame #3

# CORE: frame select 1
# CORE: ip = 0x40000000000001f0
# CORE-NEXT: r12 = 0x200000000080f3e0
# CORE-NEXT: bsp = 0x20000000008001f0
# CORE-NEXT: cfm = 0x0000000000000206
# CORE: r32 = 0x0000000000000005
# CORE-NEXT: r33 = 0x0000000000000006
# CORE-NEXT: r34 = 0x0000000000000185
# CORE-NEXT: r35 = 0x40000000000001a0
# CORE-NEXT: r36 = 0x0000000000000005
# CORE-NEXT: r37 = 0x0000000000000006
# CORE-NEXT: r38 = error: unavailable

# CORE: frame select 2
# CORE: ip = 0x40000000000001a0
# CORE-NEXT: r12 = 0x200000000080f400
# CORE-NEXT: bsp = 0x20000000008001d8
# CORE-NEXT: cfm = 0x0000000000000185
# CORE: r32 = 0x0000000000000000
# CORE-NEXT: r33 = 0x0000000000000000
# CORE-NEXT: r34 = 0x0000000000000000
# CORE-NEXT: r35 = 0x0000000000000005
# CORE-NEXT: r36 = 0x0000000000000006
# CORE-NEXT: r37 = error: unavailable

	.text
	.globl	_start
	.proc	_start
_start:
	.prologue
	.save	ar.pfs, r32
	alloc	r32 = ar.pfs, 0, 3, 2, 0
	.save	rp, r33
	mov	r33 = b0
	mov	r34 = r0 ;;
	.body
	mov	r35 = 5
	mov	r36 = 6
	br.call.sptk.many b0 = middle ;;
	mov	b0 = r33
	mov	ar.pfs = r32
	br.ret.sptk.many b0 ;;
	.endp	_start

	.globl	middle
	.proc	middle
middle:
	.prologue
	.save	ar.pfs, r34
	alloc	r34 = ar.pfs, 2, 2, 2, 0
	.fframe	32
	adds	r12 = -32, r12
	mov	r35 = b0 ;;
	adds	r14 = 16, r12 ;;
	.savesp	rp, 16
	st8	[r14] = r35
	nop.i	0 ;;
	.body
	mov	r36 = r32
	mov	r37 = r33
	br.call.sptk.many b0 = leaf ;;
	adds	r14 = 16, r12 ;;
	ld8	r35 = [r14]
	mov	ar.pfs = r34 ;;
	mov	b0 = r35
	.restore sp
	adds	r12 = 32, r12
	br.ret.sptk.many b0 ;;
	.endp	middle

# A function that saves nothing needs no unwind information, and this one has
# none.
	.globl	leaf
	.type	leaf,@function
leaf:
	add	r14 = r32, r33 ;;
	ld8	r8 = [r0]
	nop.i	0 ;;
	br.ret.sptk.many b0 ;;
	.size	leaf, .-leaf
