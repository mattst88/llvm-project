# REQUIRES: ia64-registered-target

# An IA-64 instruction is a slot of a 16-byte bundle, and its address is the
# bundle's plus the number of the slot.  Check that lldb lists the slots at
# those addresses, that it decodes every one of them with the template of its
# bundle, and that the long instruction of an MLX bundle is slots 1 and 2
# together.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t
# RUN: %lldb -b %t -o "disassemble -n main" | FileCheck %s
# RUN: %lldb -b %t -o "disassemble -b -n main" \
# RUN:   | FileCheck --check-prefix=BYTES %s
# RUN: %lldb -b %t -o "disassemble -s 0x10 -c 1" \
# RUN:   | FileCheck --check-prefix=ONE %s

	.text
	.globl	main
	.type	main,@function
main:
	alloc r34 = ar.pfs, 2, 2, 0, 0
	adds r14 = 8, r32
	add r15 = r33, r32 ;;
	ld8 r8 = [r14]
	movl r9 = 0x123456789abcdef0 ;;
	setf.sig f6 = r8
	add r8 = r8, r9
	br.ret.sptk.many b0 ;;
	.size	main, .-main

# CHECK:      <+0>:  alloc  r34 = ar.pfs, 0, 4, 0, 0
# CHECK-NEXT: <+1>:  adds   r14 = 8, r32
# CHECK-NEXT: <+2>:  add    r15 = r33, r32 ;;
# CHECK-NEXT: <+16>: ld8    r8 = [r14]
# CHECK-NEXT: <+17>: movl   r9 = 1311768467463790320 ;;
# CHECK-NEXT: <+32>: setf.sig f6 = r8
# CHECK-NEXT: <+33>: add    r8 = r8, r9
# CHECK-NEXT: <+34>: br.ret.sptk.many b0 ;;

# The bytes of a slot do not sit at its address, so what is listed with a
# slot is only as many bytes of the bundle as its address stands for.

# BYTES:      <+0>:  01 alloc
# BYTES-NEXT: <+1>:  10 adds
# BYTES-NEXT: <+2>:  11 08 80 05 e0 40 80 00 42 e0 11 02 01 80 add
# BYTES-NEXT: <+16>: 05 ld8
# BYTES-NEXT: <+17>: 40 00 1c 18 90 9a 78 56 34 12 20 01 97 f7 66 movl
# BYTES-NEXT: <+32>: 11 setf.sig
# BYTES-NEXT: <+33>: 30 add
# BYTES-NEXT: <+34>: 20 00 e1 18 80 40 24 00 40 80 08 00 84 00 br.ret.sptk.many

# One instruction is not decoded from fewer bytes than its bundle has.

# ONE:     <+16>: ld8    r8 = [r14]
# ONE-NOT: movl
