# REQUIRES: ia64
## A load of an address from the linkage table that is marked as one the
## linker may relax: an R_IA64_LTOFF22X on the add that finds the entry and an
## R_IA64_LDXMOV on the load through it. Where the symbol is this module's
## own and within reach of gp, the add gives its address, the load becomes a
## move and there is no entry.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld --defsym=abs=0x1234 a.o b.so -o exe
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=DIS
# RUN: llvm-readelf -S -r -s -x .got exe | FileCheck %s --check-prefix=EXE

# RUN: ld.lld -pie --defsym=abs=0x1234 a.o b.so -o pie
# RUN: llvm-objdump -d --no-show-raw-insn pie | FileCheck %s --check-prefix=DIS
# RUN: llvm-readelf -r pie | FileCheck %s --check-prefix=PIE

## --no-relax leaves every pair as it is.
# RUN: ld.lld --no-relax --defsym=abs=0x1234 a.o b.so -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn norelax | FileCheck %s --check-prefix=NORELAX
# RUN: llvm-readelf -S norelax | FileCheck %s --check-prefix=NORELAX-SEC

## Nothing but relaxed pairs: no linkage table entry is left.
# RUN: ld.lld --defsym=ext=0 --defsym=abs=0 --gc-sections -e near_only a.o -o none
# RUN: llvm-readelf -S none | FileCheck %s --check-prefix=NONE

## near, near + 8 and code are within reach of gp. far is 4 MiB away, ext is
## the shared object's and abs has no place in the image: those keep their
## entries, and far shares the one a plain @ltoff asks for.
# DIS-LABEL: <_start>:
# DIS-NEXT:    addl r14 = -16, r1
# DIS-NEXT:    addl r15 = -8, r1
# DIS-NEXT:    addl r16 = 16, r1 ;;
# DIS-NEXT:    mov r17 = r14
# DIS-NEXT:    nop.m 0
# DIS-NEXT:    nop.i 0
# DIS-NEXT:    ld8 r16 = [r16] ;;
# DIS-NEXT:    addl r14 = 0, r1
# DIS-NEXT:    addl r15 = 8, r1
# DIS-NEXT:    addl r16 = 16, r1 ;;
# DIS-NEXT:    ld8 r14 = [r14]
# DIS-NEXT:    nop.i 0
# DIS-NEXT:    ld8 r15 = [r15]
# DIS-NEXT:    cmp.eq p6, p7 = r0, r0 ;;
# DIS-NEXT:    addl r14 = -{{[0-9]+}}, r1
## A plain @ltoff of what a relaxed pair refers to still has its entry.
# DIS-NEXT:    addl r15 = 24, r1 ;;
# DIS-NEXT:    (p6) mov r18 = r14
# DIS:       <near_only>:
# DIS-NEXT:    addl r14 = -16, r1 ;;
# DIS-NEXT:    nop.m 0

# EXE:      .data PROGBITS 40000000000203c0
# EXE:      .got PROGBITS 40000000000203d0 {{[0-9a-f]+}} 000020
# EXE:      R_IA64_DIR64LSB 0000000000000000 ext + 0
# EXE:      40000000000203c0 0 NOTYPE LOCAL DEFAULT {{.*}} near
# EXE:      4000000000420410 0 NOTYPE LOCAL DEFAULT {{.*}} far
# EXE:      40000000000203d0 0 NOTYPE LOCAL HIDDEN {{.*}} __gp
# EXE:      Hex dump of section '.got':
# EXE-NEXT: 0x40000000000203d0 00000000 00000000 34120000 00000000
# EXE-NEXT: 0x40000000000203e0 10044200 00000040 c0030200 00000040

## Only the entries that are left are relocated.
# PIE:      contains 3 entries
# PIE:      R_IA64_REL64LSB 420460
# PIE-NEXT: R_IA64_REL64LSB 20410
# PIE-NEXT: R_IA64_DIR64LSB 0000000000000000 ext + 0

# NORELAX-LABEL: <_start>:
# NORELAX-NEXT:    addl r14 = 0, r1
# NORELAX-NEXT:    addl r15 = 8, r1
# NORELAX-NEXT:    addl r16 = 16, r1 ;;
# NORELAX-NEXT:    ld8 r17 = [r14]
# NORELAX-NEXT:    ld8 r15 = [r15]
# NORELAX-NEXT:    nop.i 0
# NORELAX-NEXT:    ld8 r16 = [r16] ;;
# NORELAX:         addl r14 = 40, r1
# NORELAX:         (p6) ld8 r18 = [r14]
# NORELAX-SEC:   .got PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000030

## The section stays, as there was no telling it would be empty.
# NONE: .got PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000000

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        addl r14 = @ltoffx(near), r1
        addl r15 = @ltoffx(near + 8), r1
        addl r16 = @ltoffx(far), r1 ;;
        ld8.mov r17 = [r14], near
        ld8.mov r15 = [r15], near + 8
        ld8.mov r16 = [r16], far ;;
        addl r14 = @ltoffx(ext), r1
        addl r15 = @ltoffx(abs), r1
        addl r16 = @ltoff(far), r1 ;;
        ld8.mov r14 = [r14], ext
        ld8.mov r15 = [r15], abs
        cmp.eq p6, p7 = r0, r0 ;;
        addl r14 = @ltoffx(code), r1
        addl r15 = @ltoff(near), r1 ;;
        (p6) ld8.mov r18 = [r14], code
        nop.i 0 ;;
code:
        br.ret.sptk.many b0 ;;
        .endp _start

        .section .text.near_only, "ax", @progbits
        .global near_only
        .proc near_only
near_only:
        addl r14 = @ltoffx(near), r1 ;;
        ld8.mov r14 = [r14], near
        br.ret.sptk.many b0 ;;
        .endp near_only

        .data
near:
        data8 1
        data8 2

        .bss
        .space 0x400000
far:
        .space 8

#--- b.s
        .data
        .global ext
        .type ext, @object
        .size ext, 8
ext:
        .space 8
