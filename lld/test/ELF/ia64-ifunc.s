# REQUIRES: ia64
## The psABI has no relocation that calls a resolver, and the dynamic loader
## of no system has support for one.

# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: not ld.lld %t.o -o /dev/null 2>&1 | FileCheck %s
# RUN: not ld.lld -shared %t.o -o /dev/null 2>&1 | FileCheck %s

# CHECK:      error: relocation R_IA64_PCREL21B against STT_GNU_IFUNC symbol 'ifn', which IA-64 does not support
# CHECK-NEXT: >>> defined in {{.*}}.o
# CHECK-NEXT: >>> referenced by {{.*}}.o:(_start)
# CHECK:      error: relocation R_IA64_FPTR64LSB against STT_GNU_IFUNC symbol 'ifn', which IA-64 does not support
# CHECK-NOT:  error:

        .text
        .type ifn, @gnu_indirect_function
ifn:
        br.ret.sptk.many b0 ;;

        .global _start
        .proc _start
_start:
        br.call.sptk.many b0 = ifn ;;
        .endp _start

        .data
        data8 @fptr(ifn)
