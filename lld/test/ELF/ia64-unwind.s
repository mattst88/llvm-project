# REQUIRES: ia64
## The unwind table: an entry for each function, of its first address, its
## last, and where its unwind information is, each as an offset from the start
## of the segment the table is in. The dynamic loader and the unwinder find
## the table from PT_IA_64_UNWIND and search it, so the entries are in the
## order of the code they describe whatever the order of the input.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o

# RUN: ld.lld a.o b.o -o exe
# RUN: llvm-readelf -lSW exe | FileCheck %s --check-prefix=HDR
# HDR:      .IA_64.unwind      IA_64_UNWIND    4000000000000120 000120 000048 00  AL  3   0  8
# HDR-NEXT: .IA_64.unwind_info PROGBITS        4000000000000168 000168 000030 00   A  0   0  8
# HDR-NEXT: .text              PROGBITS
# HDR:      LOAD           0x000000 0x4000000000000000 0x4000000000000000 {{.*}} R E 0x10000
# HDR:      IA_64_UNWIND   0x000120 0x4000000000000120 0x4000000000000120 0x000048 0x000048 R   0x8
# RUN: llvm-nm -n exe | FileCheck %s --check-prefix=NM
# RUN: llvm-readelf -x .IA_64.unwind exe | FileCheck %s --check-prefix=TABLE

## The table follows the code that was moved, and loses the entry of code
## that was dropped.
# RUN: echo 'h' > order
# RUN: ld.lld --symbol-ordering-file=order a.o b.o -o ordered
# RUN: llvm-nm -n ordered | FileCheck %s --check-prefix=ORDERED-NM
# RUN: llvm-readelf -x .IA_64.unwind ordered | FileCheck %s --check-prefix=ORDERED
# RUN: ld.lld --gc-sections a.o b.o -o gc
# RUN: llvm-nm -n gc | FileCheck %s --check-prefix=GC-NM
# RUN: llvm-readelf -x .IA_64.unwind gc | FileCheck %s --check-prefix=GC

# RUN: ld.lld -shared a.o b.o -o so
# RUN: llvm-readelf -lW so | FileCheck %s --check-prefix=SO
# SO: IA_64_UNWIND   0x000200 0x0000000000000200 0x0000000000000200 0x000048 0x000048 R   0x8

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.call.sptk.many b0 = g ;;
        br.ret.sptk.many b0 ;;
        .endp _start

        .section .text.g, "ax", @progbits
        .global g
        .proc g
g:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp g

#--- b.s
        .section .text.h, "ax", @progbits
        .global h
        .proc h
h:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        br.ret.sptk.many b0 ;;
        .endp h

#--- end

# NM: 4000000000000000 r __gp
# NM-NEXT: 40000000000001a0 T _start
# NM-NEXT: 40000000000001c0 T g
# NM-NEXT: 40000000000001d0 T h

# TABLE: Hex dump of section '.IA_64.unwind':
# TABLE-NEXT: 0x4000000000000120 a0010000 00000000 c0010000 00000000 ................
# TABLE-NEXT: 0x4000000000000130 68010000 00000000 c0010000 00000000 h...............
# TABLE-NEXT: 0x4000000000000140 d0010000 00000000 78010000 00000000 ........x.......
# TABLE-NEXT: 0x4000000000000150 d0010000 00000000 f0010000 00000000 ................
# TABLE-NEXT: 0x4000000000000160 88010000 00000000                   ........

# ORDERED-NM: 4000000000000000 r __gp
# ORDERED-NM-NEXT: 40000000000001a0 T h
# ORDERED-NM-NEXT: 40000000000001c0 T _start
# ORDERED-NM-NEXT: 40000000000001e0 T g

# ORDERED: Hex dump of section '.IA_64.unwind':
# ORDERED-NEXT: 0x4000000000000120 a0010000 00000000 c0010000 00000000 ................
# ORDERED-NEXT: 0x4000000000000130 88010000 00000000 c0010000 00000000 ................
# ORDERED-NEXT: 0x4000000000000140 e0010000 00000000 68010000 00000000 ........h.......
# ORDERED-NEXT: 0x4000000000000150 e0010000 00000000 f0010000 00000000 ................
# ORDERED-NEXT: 0x4000000000000160 78010000 00000000                   x.......

# GC-NM: 4000000000000000 r __gp
# GC-NM-NEXT: 4000000000000170 T _start
# GC-NM-NEXT: 4000000000000190 T g

# GC: Hex dump of section '.IA_64.unwind':
# GC-NEXT: 0x4000000000000120 70010000 00000000 90010000 00000000 p...............
# GC-NEXT: 0x4000000000000130 50010000 00000000 90010000 00000000 P...............
# GC-NEXT: 0x4000000000000140 a0010000 00000000 60010000 00000000 ........`.......
