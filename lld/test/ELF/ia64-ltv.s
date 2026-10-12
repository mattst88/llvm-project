# REQUIRES: ia64
## @ltv is the address the linker gave, which is what is wanted wherever the
## image is loaded, so it takes no dynamic relocation. The 32-bit form holds
## it only where the address fits: GNU ld cuts it short without a word.

# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu --defsym WORD=1 %s -o %t.word.o

# RUN: ld.lld -pie %t.word.o -o %t.pie
# RUN: llvm-readelf -r -s -x .data %t.pie | FileCheck %s --check-prefix=PIE
# PIE: There are no relocations in this file.
# PIE: 0000000000000210 0 NOTYPE GLOBAL DEFAULT {{.*}} _start
# PIE: 0x{{[0-9a-f]+}} 10020000 00000000 10020000

# RUN: ld.lld %t.o -o %t.exe
# RUN: llvm-readelf -r -x .data %t.exe | FileCheck %s --check-prefix=EXE
# EXE: There are no relocations in this file.
# EXE: 0x{{[0-9a-f]+}} 20010000 00000040

## An executable is at 0x4000000000000000.
# RUN: not ld.lld %t.word.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
# ERR: error: {{.*}}.o:(.data+0x8): relocation R_IA64_LTV32LSB out of range: 4611686018427388192 is not in [-2147483648, 4294967295]; references '_start'

        .text
        .global _start
_start:
        br.ret.sptk.many b0 ;;
        .data
        data8 @ltv(_start)
.ifdef WORD
        data4 @ltv(_start)
.endif
