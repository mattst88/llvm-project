# REQUIRES: ia64-registered-target, lld

# The table that names every architecture's jump-slot relocation needs an
# IA-64 entry, or reading the symbols of a dynamically linked IA-64 binary
# asserts.  Check that the PLT entries of one get their trampoline symbols:
# after the three bundles that every entry enters the dynamic linker through
# come three bundles for each function, two that call it and one that
# resolves it.  The symbol of a function is at its own three bundles, and
# the functions are in the order of their relocations.

# RUN: llvm-mc -filetype=obj --triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld -shared --section-start=.plt=0x10000 %t.o -o %t
# RUN: %lldb -b %t -o "image dump symtab" | FileCheck %s

# CHECK: Trampoline 0x0000000000010030 0x0000000000000030 {{.*}} first
# CHECK-NEXT: Trampoline 0x0000000000010060 0x0000000000000030 {{.*}} second
# CHECK-NOT: Trampoline

	.text
	.globl	caller
	.type	caller,@function
caller:
	br.call.sptk.many b0 = first# ;;
	br.call.sptk.many b0 = second# ;;
	br.ret.sptk.many b0 ;;
	.size	caller, .-caller
