# REQUIRES: ia64-registered-target, lld

# Unwind descriptors that the assembler's directives do not make, written
# out as bytes. Each function is three bundles, which is nine slots.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: yaml2obj %S/Inputs/ia64-unwind-info.yaml -o %t.core

# priunat_when_mem is the time the primary ar.unat is saved to the memory
# stack, and priunat_psprel is where. The time is of that register and of no
# other: rp is in b0 throughout. The primary ar.unat is not one of the
# registers of a row, so the row of the second slot, from which it is saved,
# reads as the first does.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n when_mem" \
# RUN:   | FileCheck %s --check-prefix=WHEN-MEM

# WHEN-MEM: object file UnwindPlan:
# WHEN-MEM-NEXT: This UnwindPlan originally sourced from IA-64 unwind info
# WHEN-MEM: row[0]: 0: CFA=r12 +0 AFA=
# WHEN-MEM-SAME: => r12=CFA+0 ip=b0 cfm={{.*}} bsp=AFA+0{{ *$}}
# WHEN-MEM-NEXT: row[1]: 2: CFA=r12 +0 AFA=
# WHEN-MEM-SAME: => r12=CFA+0 ip=b0 cfm={{.*}} bsp=AFA+0{{ *$}}
# WHEN-MEM-NOT: row[2]:
# WHEN-MEM: Arch default UnwindPlan:

# A region is given a length, and ends no later than its function does. A
# prologue that is given a hundred slots in a function of nine is the whole
# of the function: the frame is made in its second slot, and nothing is at
# a slot past the last.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n long_region" \
# RUN:   | FileCheck %s --check-prefix=LONG

# LONG: object file UnwindPlan:
# LONG-NEXT: This UnwindPlan originally sourced from IA-64 unwind info
# LONG: row[0]: 0: CFA=r12 +0 AFA=
# LONG-NEXT: row[1]: 2: CFA=r12+32 AFA=
# LONG-NOT: row[2]:
# LONG: Arch default UnwindPlan:

# The spill mask of a prologue has two bits for each of its slots. One whose
# prologue is given more slots than the descriptors have room for the bits
# of is not to be read at all, however long the function is said to be, and
# the function has no plan from its unwind information.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n long_mask" \
# RUN:   | FileCheck %s --check-prefix=MASK

# MASK: UNWIND PLANS for {{.*}}`long_mask
# MASK-NOT: object file UnwindPlan:
# MASK: Arch default UnwindPlan:

# label_state gives the state a number, and copy_state goes back to the one
# with the number. A function that returns from seven places has its frame
# at each of them: every body but the first copies the state the one before
# it gave a label to, and then gives it a label of its own.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n copies" \
# RUN:   | FileCheck %s --check-prefix=COPIES

# COPIES: object file UnwindPlan:
# COPIES-NEXT: This UnwindPlan originally sourced from IA-64 unwind info
# COPIES: row[0]: 0: CFA=r12 +0 AFA=
# COPIES-NEXT: row[1]: 1: CFA=r12+32 AFA=
# COPIES-NOT: row[2]:
# COPIES: Arch default UnwindPlan:

# A thousand copies of four states are a thousand copies, and take no time
# to speak of.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n many_copies" \
# RUN:   | FileCheck %s --check-prefix=MANY

# MANY: object file UnwindPlan:
# MANY-NEXT: This UnwindPlan originally sourced from IA-64 unwind info
# MANY: row[0]: 0: CFA=r12 +0 AFA=
# MANY-NEXT: row[1]: 1: CFA=r12+32 AFA=
# MANY-NOT: row[2]:
# MANY: Arch default UnwindPlan:

# A state that has no label yet cannot be copied.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n no_label" \
# RUN:   | FileCheck %s --check-prefix=NO-LABEL

# NO-LABEL: UNWIND PLANS for {{.*}}`no_label
# NO-LABEL-NOT: object file UnwindPlan:
# NO-LABEL: Arch default UnwindPlan:

# The descriptors are read again for each slot at which what is saved
# changes. Those of too_much, 24000 regions of a slot each, would be read
# 24000 times, and are given up on: the function has no plan from them.

# RUN: %lldb -b %t -c %t.core -o "target modules load -f %t -s 0" \
# RUN:   -o "image show-unwind -n too_much" \
# RUN:   | FileCheck %s --check-prefix=TOO-MUCH

# TOO-MUCH: UNWIND PLANS for {{.*}}`too_much
# TOO-MUCH-NOT: object file UnwindPlan:
# TOO-MUCH: Arch default UnwindPlan:

	.text
	.globl	_start
	.type	_start,@function
_start:
	nop.m	0
	nop.i	0
	br.ret.sptk.many b0 ;;
	.size	_start, .-_start

	.macro	function name
	.globl	\name
	.type	\name,@function
\name:
	nop.m	0
	nop.i	0
	nop.i	0 ;;
	nop.m	0
	nop.i	0
	nop.i	0 ;;
	nop.m	0
	nop.i	0
	br.ret.sptk.many b0 ;;
.L\name\()_end:
	.size	\name, .-\name
	.endm

	function when_mem
	function long_region
	function long_mask
	function copies
	function many_copies
	function no_label

	.globl	too_much
	.type	too_much,@function
too_much:
	.rept	8000
	nop.m	0
	nop.i	0
	nop.i	0 ;;
	.endr
.Ltoo_much_end:
	.size	too_much, .-too_much

	.section .IA_64.unwind_info,"a",@progbits
# A prologue of six slots, with priunat_psprel(4) and priunat_when_mem(1),
# and a body of three.
.Lwhen_mem_info:
	data8	0x1000000000001
	.byte	0x06, 0xf0, 17, 4, 0xf0, 19, 1, 0x23

# A prologue of a hundred slots, with mem_stack_f(1, 32) and rp_gr(40), and
# a body of three.
.Llong_region_info:
	data8	0x1000000000001
	.byte	0x60, 100, 0xe0, 1, 2, 0xb0, 0xa8, 0x23

# A prologue of 0x3fffffff slots, with gr_mem(r4) and a spill mask.
.Llong_mask_info:
	data8	0x1000000000002
	.byte	0x60, 0xff, 0xff, 0xff, 0xff, 0x03, 0xd1, 0xb8
	.byte	0x80, 0, 0, 0, 0, 0, 0, 0

# A prologue of two slots with mem_stack_f(0, 32), and seven bodies of a
# slot each that end with an epilogue: label_state(1) in the first, and
# copy_state(n - 1) and label_state(n) in the others.
.Lcopies_info:
	data8	0x1000000000005
	.byte	0x02, 0xe0, 0, 2
	.byte	0x21, 0x81, 0xc0, 0
	.byte	0x21, 0xa1, 0x82, 0xc0, 0
	.byte	0x21, 0xa2, 0x83, 0xc0, 0
	.byte	0x21, 0xa3, 0x84, 0xc0, 0
	.byte	0x21, 0xa4, 0x85, 0xc0, 0
	.byte	0x21, 0xa5, 0x86, 0xc0, 0
	.byte	0x21, 0xa6, 0x87, 0xc0, 0
	.byte	0, 0

# The same prologue, and one body with label_state(n) and 250 of
# copy_state(n), for n from 1 to 4.
.Lmany_copies_info:
	data8	0x100000000007f
	.byte	0x02, 0xe0, 0, 2, 0x27
	.byte	0x81
	.fill	250, 1, 0xa1
	.byte	0x82
	.fill	250, 1, 0xa2
	.byte	0x83
	.fill	250, 1, 0xa3
	.byte	0x84
	.fill	250, 1, 0xa4
	.fill	7, 1, 0

# The same prologue, and a body with copy_state(1) before label_state(1).
.Lno_label_info:
	data8	0x1000000000001
	.byte	0x02, 0xe0, 0, 2, 0x27, 0xa1, 0x81, 0

.Ltoo_much_info:
	data8	0x1000000000bb8
	.fill	24000, 1, 0x21

	.section .IA_64.unwind,"a",@progbits
	data8	@segrel(when_mem), @segrel(.Lwhen_mem_end), @segrel(.Lwhen_mem_info)
	data8	@segrel(long_region), @segrel(.Llong_region_end), @segrel(.Llong_region_info)
	data8	@segrel(long_mask), @segrel(.Llong_mask_end), @segrel(.Llong_mask_info)
	data8	@segrel(copies), @segrel(.Lcopies_end), @segrel(.Lcopies_info)
	data8	@segrel(many_copies), @segrel(.Lmany_copies_end), @segrel(.Lmany_copies_info)
	data8	@segrel(no_label), @segrel(.Lno_label_end), @segrel(.Lno_label_info)
	data8	@segrel(too_much), @segrel(.Ltoo_much_end), @segrel(.Ltoo_much_info)
