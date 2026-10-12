# REQUIRES: ia64
## The unwind table entry of a function in a section group that another file
## has already supplied. The entry goes with the function, whether its section
## is a member of the group, as the assemblers make it, or only names the
## function's section as the one it is ordered by. What is left is in the
## order of the code.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu c.s -o c.o
# RUN: llvm-readelf -g b.o | FileCheck %s --check-prefix=GROUP

# GROUP:      COMDAT group section [{{.*}}] `.group' [f] contains
# GROUP:      .text.f
# GROUP-NEXT: .IA_64.unwind_info.text.f
# GROUP-NEXT: .IA_64.unwind.text.f
# GROUP-NEXT: .rela.IA_64.unwind.text.f

# RUN: ld.lld a.o b.o -o grouped
# RUN: llvm-nm -n grouped | FileCheck %s --check-prefix=NM
# RUN: llvm-readelf -x .IA_64.unwind grouped | FileCheck %s --check-prefix=TABLE

## Three functions, three entries.
# NM:      40000000000001a0 T _start
# NM-NEXT: 40000000000001b0 T f
# NM-NEXT: 40000000000001c0 T other

# TABLE:      0x4000000000000120 a0010000 00000000 b0010000 00000000
# TABLE-NEXT: 0x4000000000000130 68010000 00000000 b0010000 00000000
# TABLE-NEXT: 0x4000000000000140 c0010000 00000000 78010000 00000000
# TABLE-NEXT: 0x4000000000000150 c0010000 00000000 d0010000 00000000
# TABLE-NEXT: 0x4000000000000160 88010000 00000000 {{ *}}
# TABLE-NOT:  0x

# RUN: ld.lld a.o c.o -o ordered
# RUN: llvm-nm -n ordered | FileCheck %s --check-prefix=ORDERED-NM
# RUN: llvm-readelf -x .IA_64.unwind ordered | FileCheck %s --check-prefix=ORDERED

# ORDERED-NM:      40000000000001b0 T _start
# ORDERED-NM-NEXT: 40000000000001c0 T f
# ORDERED-NM-NEXT: 40000000000001d0 T other

# ORDERED:      0x4000000000000120 b0010000 00000000 c0010000 00000000
# ORDERED-NEXT: 0x4000000000000130 68010000 00000000 c0010000 00000000
# ORDERED-NEXT: 0x4000000000000140 d0010000 00000000 78010000 00000000
# ORDERED-NEXT: 0x4000000000000150 d0010000 00000000 e0010000 00000000
# ORDERED-NEXT: 0x4000000000000160 88010000 00000000 {{ *}}
# ORDERED-NOT:  0x

## A relocatable link drops the entry and its relocations alike.
# RUN: ld.lld -r a.o c.o -o ordered.ro
# RUN: llvm-readelf -SW ordered.ro | FileCheck %s --check-prefix=RELOC
# RELOC:     .IA_64.unwind.text.f IA_64_UNWIND {{.*}} 000018 00 ALG
# RELOC-NOT: .IA_64.unwind.text.f IA_64_UNWIND

## An entry in a table that stays is another matter: nothing says which
## function it belongs to, and GNU ld refuses it as well.
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu d.s -o d.o
# RUN: not ld.lld a.o d.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
# ERR: error: relocation refers to a discarded section: .text.f
# ERR: >>> referenced by d.o:(.IA_64.unwind+0x{{.*}})

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp _start

        .section .text.f, "axG", @progbits, f, comdat
        .global f
        .proc f
f:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp f

#--- b.s
        .text
        .global other
        .proc other
other:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp other

        .section .text.f, "axG", @progbits, f, comdat
        .global f
        .proc f
f:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp f

#--- c.s
        .text
        .global other
        .proc other
other:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp other

        .section .text.f, "axG", @progbits, f, comdat
        .global f
f:
        alloc r32 = ar.pfs, 0, 1, 0, 0
        br.ret.sptk.many b0 ;;
.Lend:
        .section .IA_64.unwind_info.f, "a", @progbits
.Linfo:
        data8 0x1000000000001, 0xe6
        .section .IA_64.unwind.text.f, "ao", @unwind, .text.f
        data8 @segrel(f), @segrel(.Lend), @segrel(.Linfo)

#--- d.s
        .text
        .global other
other:
        br.ret.sptk.many b0 ;;

        .section .text.f, "axG", @progbits, f, comdat
        .global f
f:
        br.ret.sptk.many b0 ;;
.Lend:
        .section .IA_64.unwind_info.f, "a", @progbits
.Linfo:
        data8 0x1000000000001, 0xe6
        .section .IA_64.unwind, "ao", @unwind, .text
        data8 @segrel(f), @segrel(.Lend), @segrel(.Linfo)
