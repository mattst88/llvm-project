# RUN: split-file %s %t
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/le.o %t/le.s
# RUN: not llvm-jitlink -noexec %t/le.o 2>&1 | FileCheck %s --check-prefix=LE
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/ie.o %t/ie.s
# RUN: not llvm-jitlink -noexec %t/ie.o 2>&1 | FileCheck %s --check-prefix=IE
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/gd.o %t/gd.s
# RUN: not llvm-jitlink -noexec %t/gd.o 2>&1 | FileCheck %s --check-prefix=GD
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/ld.o %t/ld.s
# RUN: not llvm-jitlink -noexec %t/ld.o 2>&1 | FileCheck %s --check-prefix=LD
# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t/data.o \
# RUN:   %t/data.s
# RUN: not llvm-jitlink -noexec %t/data.o 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DATA

# Thread-local storage is not done: there is no thread pointer offset and no
# module for JIT'd code. Each of the four models is refused with an error that
# says so and names the variable.

# LE: R_IA64_TPREL14 is for thread-local storage, which is not supported for ia64 (the variable is tls); -femulated-tls makes code that needs none
# IE: R_IA64_LTOFF_TPREL22 is for thread-local storage, which is not supported for ia64 (the variable is tls);
# GD: R_IA64_LTOFF_DTP{{MOD|REL}}22 is for thread-local storage, which is not supported for ia64 (the variable is tls);
# LD: R_IA64_{{LTOFF_DTPMOD22|DTPREL22}} is for thread-local storage, which is not supported for ia64 (the variable is tls);
# DATA: R_IA64_{{TPREL64LSB|DTPMOD64LSB|DTPREL64LSB}} is for thread-local storage, which is not supported for ia64 (the variable is tls);

#--- le.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	adds	r14 = @tprel(tls), r13
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.section .tdata,"awT",@progbits
tls:
	data8	1

#--- ie.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	addl	r14 = @ltoff(@tprel(tls)), r1
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.section .tdata,"awT",@progbits
tls:
	data8	1

#--- gd.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	addl	r32 = @ltoff(@dtpmod(tls)), r1
	addl	r33 = @ltoff(@dtprel(tls)), r1
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.section .tdata,"awT",@progbits
tls:
	data8	1

#--- ld.s
	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	addl	r32 = @ltoff(@dtpmod(tls)), r1
	addl	r33 = @dtprel(tls), r0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

	.section .tdata,"awT",@progbits
tls:
	data8	1

#--- data.s
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

	.data
	.globl	slots
	.type	slots,@object
slots:
	data8	@tprel(tls)
	data8	@dtpmod(tls)
	data8	@dtprel(tls)
	.size	slots, .-slots

	.section .tdata,"awT",@progbits
tls:
	data8	1
