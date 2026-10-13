# RUN: llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o %s
# RUN: llvm-jitlink -noexec -abs ext=0x7000000 -check %s %t.o

# A call of a function of another graph. Only the descriptor of the function
# is known, so the call is of a stub that loads the address of the descriptor
# from the linkage table and enters the function with its global pointer.

	.text
	.globl	main
	.type	main,@function
	.proc	main
main:
	.globl	main_code
	.type	main_code,@object
main_code:
# jitlink-check: (*{8}(main_code + 8))[55:36] = (stub_addr(ELF_ia64_stubs.s.tmp.o, ext) - main_code)[23:4]
# jitlink-check: (*{8}(main_code + 8))[59:59] = (stub_addr(ELF_ia64_stubs.s.tmp.o, ext) - main_code)[24:24]
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = ext ;;
# A second call is of the same stub.
# jitlink-check: (*{8}(main_code + 24))[55:36] = (stub_addr(ELF_ia64_stubs.s.tmp.o, ext) - (main_code + 16))[23:4]
	nop.m	0
	nop.m	0
	br.call.sptk.many b0 = ext ;;
	nop.m	0
	nop.m	0
	br.ret.sptk.many b0 ;;
	.endp	main
	.size	main, .-main

# The entry holds the descriptor, and the first instruction of the stub has
# the offset of the entry from the global pointer.
# jitlink-check: *{8}(got_addr(ELF_ia64_stubs.s.tmp.o, ext)) = ext
# jitlink-check: section_addr(ELF_ia64_stubs.s.tmp.o, $__GOT) + (*{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext)))[24:18] = got_addr(ELF_ia64_stubs.s.tmp.o, ext)

# The rest of the stub is as it was made:
#   addl r15 = @ltoff(descriptor), r1 ;; ld8 r15 = [r15] ; nop.i 0 ;;
#   ld8 r16 = [r15], 8 ;; ld8 r1 = [r15] ; mov b6 = r16 ;;
#   nop.m 0 ; nop.i 0 ; br b6 ;;
# jitlink-check: (*{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext)))[17:0] = 0x780b
# jitlink-check: (*{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext)))[63:42] = 0x3c09
# jitlink-check: *{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext) + 8) = 0x000400000020303c
# jitlink-check: *{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext) + 16) = 0x001014181e20800b
# jitlink-check: *{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext) + 24) = 0x07000900c020303c
# jitlink-check: *{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext) + 32) = 0x0000000100000011
# jitlink-check: *{8}(stub_addr(ELF_ia64_stubs.s.tmp.o, ext) + 40) = 0x0080006000000200
