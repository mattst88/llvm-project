# REQUIRES: ia64
## A branch reaches 16 MiB either way. A call that is to go further goes to
## a thunk that is within reach, and the thunk is a brl, which reaches
## anywhere and needs no register.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: ld.lld -T far.t a.o -o far
# RUN: llvm-nm -n far | FileCheck %s --check-prefix=NM
# RUN: llvm-objdump -d --no-show-raw-insn far | FileCheck %s --check-prefix=DIS

## What is within reach is branched to as it is.
# RUN: ld.lld -T near.t a.o -o near
# RUN: llvm-nm -n near | FileCheck %s --check-prefix=NEAR

#--- a.s
        .text
        .global _start
        .proc _start
_start:
## 16 MiB ahead of this bundle is one bundle too far. The bundles after
## this one are that much nearer, and reach.
        br.call.sptk.many b0 = high ;;
        br.call.sptk.many b0 = high ;;
        br.call.sptk.many b0 = local ;;
        br.sptk.many high ;;
        .endp _start

        .section .text.high, "ax", @progbits
        .global high
        .proc high
high:
        br.call.sptk.many b0 = _start ;;
local:
        br.ret.sptk.many b0 ;;
        .endp high

#--- far.t
SECTIONS {
  .text 0x4000000000010000 : { *(.text) }
  .text.high 0x4000000001010000 : { *(.text.high) }
}

#--- near.t
SECTIONS {
  .text 0x4000000000010000 : { *(.text) }
  .text.high 0x4000000001000000 : { *(.text.high) }
}

#--- end

# NM: 0000000000000000 t __gp
# NM-NEXT: 4000000000010000 T _start
# NM-NEXT: 4000000000010040 t __ia64_thunk_high
# NM-NEXT: 4000000001010000 T high
# NM-NEXT: 4000000001010010 t local

# DIS: Disassembly of section .text:
# DIS: 4000000000010000 <_start>:
# DIS-NEXT: 4000000000010000:      	br.call.sptk.many	b0 = 0x4000000000010040 // [BBB]
# DIS-NEXT: 4000000000010006:      	nop.b	0
# DIS-NEXT: 400000000001000c:      	nop.b	0 ;;
# DIS-NEXT: 4000000000010010:      	br.call.sptk.many	b0 = 0x4000000001010000 // [BBB]
# DIS-NEXT: 4000000000010016:      	nop.b	0
# DIS-NEXT: 400000000001001c:      	nop.b	0 ;;
# DIS-NEXT: 4000000000010020:      	nop.m	0               // [MBB]
# DIS-NEXT: 4000000000010026:      	br.call.sptk.many	b0 = 0x4000000001010010
# DIS-NEXT: 400000000001002c:      	nop.b	0 ;;
# DIS-NEXT: 4000000000010030:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000000010036:      	nop.i	0
# DIS-NEXT: 400000000001003c:      	br.cond.sptk.many	0x4000000001010000 ;;
# DIS: 4000000000010040 <__ia64_thunk_high>:
## The thunk is 64 bytes closer than the call that could not reach, so the
## long branch it was made with is relaxed to a short one.
# DIS-NEXT: 4000000000010040:      	nop.m	0               // [MBB]
# DIS-NEXT: 4000000000010046:      	nop.b	0
# DIS-NEXT: 400000000001004c:      	br.cond.sptk.many	0x4000000001010000 ;;
# DIS: Disassembly of section .text.high:
# DIS: 4000000001010000 <high>:
# DIS-NEXT: 4000000001010000:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000001010006:      	nop.i	0
# DIS-NEXT: 400000000101000c:      	br.call.sptk.many	b0 = 0x4000000000010000 ;;
# DIS: 4000000001010010 <local>:
# DIS-NEXT: 4000000001010010:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000001010016:      	nop.i	0
# DIS-NEXT: 400000000101001c:      	br.ret.sptk.many	b0 ;;

# NEAR: 0000000000000000 t __gp
# NEAR-NEXT: 4000000000010000 T _start
# NEAR-NEXT: 4000000001000000 T high
# NEAR-NEXT: 4000000001000010 t local
