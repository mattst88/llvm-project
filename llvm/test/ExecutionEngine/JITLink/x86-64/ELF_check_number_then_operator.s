# RUN: llvm-mc -triple=x86_64-unknown-linux -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -check %s %t.o

# A check expression is read left to right, one binary operator after
# another. An operator that came after a number was not found, because the
# space between the two was not skipped as it is after any other operand, so
# the expression was refused with "unexpected token ' '".

# jitlink-check: v + 8 - 8 = v
# jitlink-check: (v + 8 - 8) = v
# jitlink-check: 16 >> 4 = 1
# jitlink-check: (0x10 + 0x10 >> 4) = 2
# jitlink-check: (*{8}v)[7:0] + 1 - 1 = 0x88
# jitlink-check: *{8}(v + 4 + 4) = 0x10

# The address of a load is all of the expression that follows the size, so
# what is done with the value loaded goes outside parentheses.
# jitlink-check: *{8}v + 8 = 0x10
# jitlink-check: (*{8}w) >> 4 = 1
# jitlink-check: (*{8}w) - 16 = 0

	.text
	.globl	main
main:
	retq

	.data
	.globl	v
v:
	.quad	0x1122334455667788
	.globl	w
w:
	.quad	0x10
