# REQUIRES: ia64
## A long branch, which has a displacement of 60 bits. One that turns out to
## have no further to go than 16 MiB is made the short branch: the bundle
## keeps its first instruction and its stop.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: ld.lld -T a.t a.o -o exe
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s
# RUN: ld.lld --no-relax -T a.t a.o -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn norelax | FileCheck %s --check-prefix=NORELAX

# CHECK-LABEL: <_start>:
# CHECK-NEXT:    ld8 r14 = [r15] // [MBB]
# CHECK-NEXT:    nop.b 0
# CHECK-NEXT:    br.call.sptk.many b0 = 0x4000000000001040 ;;
# CHECK-NEXT:    nop.m 0 // [MBB]
# CHECK-NEXT:    nop.b 0
# CHECK-NEXT:    br.cond.sptk.many 0x4000000000001040
# CHECK-NEXT:    nop.m 0 // [MBB]
# CHECK-NEXT:    nop.b 0
# CHECK-NEXT:    (p6) br.cond.dptk.few 0x4000000000001040 ;;
# CHECK-NEXT:    nop.m 0 // [MLX]
# CHECK-NEXT:    brl.call.sptk.many b0 = 0x4000000002000000 ;;

# NORELAX-LABEL: <_start>:
# NORELAX-NEXT:    ld8 r14 = [r15] // [MLX]
# NORELAX-NEXT:    brl.call.sptk.many b0 = 0x4000000000001040 ;;
# NORELAX-NEXT:    nop.m 0 // [MLX]
# NORELAX-NEXT:    brl.cond.sptk.many 0x4000000000001040
# NORELAX-NEXT:    nop.m 0 // [MLX]
# NORELAX-NEXT:    (p6) brl.cond.dptk.few 0x4000000000001040 ;;
# NORELAX-NEXT:    nop.m 0 // [MLX]
# NORELAX-NEXT:    brl.call.sptk.many b0 = 0x4000000002000000 ;;

#--- a.t
SECTIONS {
  .text 0x4000000000001000 : { *(.text .text.*) }
  .far 0x4000000002000000 : { *(.far) }
}

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        ld8 r14 = [r15]
        brl.call.sptk.many b0 = near ;;
        brl.cond.sptk.many near
        (p6) brl.cond.dptk.few near ;;
        brl.call.sptk.many b0 = far ;;
        .endp _start

        .section .text.near, "ax", @progbits
near:
        br.ret.sptk.many b0 ;;

        .section .far, "ax", @progbits
far:
        br.ret.sptk.many b0 ;;
