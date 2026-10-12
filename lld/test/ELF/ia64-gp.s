# REQUIRES: ia64
## gp is the address that the short data area and the linkage table are
## reached from: 22 bits of offset, 2 MiB either way. The sections that are
## addressed from it are kept together, what is in the file and then what is
## not, and __gp is where they start.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu big.s -o big.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu none.s -o none.o

# RUN: ld.lld a.o -o exe
# RUN: llvm-readelf -SW exe | FileCheck %s --check-prefix=SEC
# RUN: llvm-nm -n exe | FileCheck %s --check-prefix=NM
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=DIS

## With more than 2 MiB to reach, gp is 2 MiB into it.
# RUN: ld.lld a.o big.o -o big
# RUN: llvm-nm -n big | FileCheck %s --check-prefix=BIG
# RUN: llvm-objdump -d --no-show-raw-insn big | FileCheck %s --check-prefix=BIG-DIS

## With nothing to reach, gp is the start of the data.
# RUN: ld.lld none.o -o none
# RUN: llvm-nm -n none | FileCheck %s --check-prefix=NONE

## A linker script may give gp a place.
# RUN: ld.lld a.o --defsym=__gp=0x4000000000030000 -o script
# RUN: llvm-nm -n script | FileCheck %s --check-prefix=SCRIPT

## What is past the reach of gp is an error.
# RUN: not ld.lld a.o --defsym=__gp=0x4000000001000000 -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=ERR

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        addl r14 = @gprel(sd), r1
        addl r15 = @gprel(sb), r1
        addl r16 = @ltoff(d), r1 ;;
        movl r14 = @gprel(d) ;;
        br.ret.sptk.many b0 ;;
        .endp _start

        .data
d:
        data8 1
        .bss
b:
        .space 8
        .section .sdata, "aws", @progbits
sd:
        data8 @gprel(sb)
        .section .sbss, "aws", @nobits
sb:
        .space 8

#--- big.s
        .section .sbss, "aws", @nobits
        .space 0x200000

#--- none.s
        .text
        .global _start
_start:
        br.ret.sptk.many b0 ;;
        .data
        data8 1

#--- end

# SEC:      [ 1] .text             PROGBITS        4000000000000120 000120 000030 00  AX  0   0 16
# SEC-NEXT: [ 2] .data             PROGBITS        4000000000010150 000150 000008 00  WA  0   0  8
# SEC-NEXT: [ 3] .sdata            PROGBITS        4000000000010158 000158 000008 00 WAp  0   0  8
# SEC-NEXT: [ 4] .got              PROGBITS        4000000000010160 000160 000008 00 WAp  0   0  8
# SEC-NEXT: [ 5] .sbss             NOBITS          4000000000010168 000168 000008 00 WAp  0   0  1
# SEC-NEXT: [ 6] .bss              NOBITS          4000000000010170 000168 000008 00  WA  0   0  1

# NM: 4000000000000120 T _start
# NM-NEXT: 4000000000010150 d d
# NM-NEXT: 4000000000010158 d __gp
# NM-NEXT: 4000000000010158 d sd
# NM-NEXT: 4000000000010168 b sb
# NM-NEXT: 4000000000010170 b b

# DIS: Disassembly of section .text:
# DIS: 4000000000000120 <_start>:
# DIS-NEXT: 4000000000000120:      	addl	r14 = 0, r1     // [MII]
# DIS-NEXT: 4000000000000126:      	addl	r15 = 16, r1
# DIS-NEXT: 400000000000012c:      	addl	r16 = 8, r1 ;;
# DIS-NEXT: 4000000000000130:      	nop.m	0               // [MLX]
# DIS-NEXT: 4000000000000136:      	movl	r14 = -8 ;;
# DIS-NEXT: 4000000000000140:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000000000146:      	nop.i	0
# DIS-NEXT: 400000000000014c:      	br.ret.sptk.many	b0 ;;

# BIG: 4000000000000120 T _start
# BIG-NEXT: 4000000000010150 d d
# BIG-NEXT: 4000000000010158 d sd
# BIG-NEXT: 4000000000010168 b sb
# BIG-NEXT: 4000000000210158 d __gp
# BIG-NEXT: 4000000000210170 b b

# BIG-DIS: Disassembly of section .text:
# BIG-DIS: 4000000000000120 <_start>:
# BIG-DIS-NEXT: 4000000000000120:      	addl	r14 = -2097152, r1 // [MII]
# BIG-DIS-NEXT: 4000000000000126:      	addl	r15 = -2097136, r1
# BIG-DIS-NEXT: 400000000000012c:      	addl	r16 = -2097144, r1 ;;
# BIG-DIS-NEXT: 4000000000000130:      	nop.m	0               // [MLX]
# BIG-DIS-NEXT: 4000000000000136:      	movl	r14 = -2097160 ;;
# BIG-DIS-NEXT: 4000000000000140:      	nop.m	0               // [MIB]
# BIG-DIS-NEXT: 4000000000000146:      	nop.i	0
# BIG-DIS-NEXT: 400000000000014c:      	br.ret.sptk.many	b0 ;;

# NONE: 4000000000000120 T _start
# NONE-NEXT: 4000000000010130 d __gp

# SCRIPT: 4000000000000120 T _start
# SCRIPT-NEXT: 4000000000010150 d d
# SCRIPT-NEXT: 4000000000010158 d sd
# SCRIPT-NEXT: 4000000000010168 b sb
# SCRIPT-NEXT: 4000000000010170 b b
# SCRIPT-NEXT: 4000000000030000 A __gp

# ERR: ld.lld: error: a.o:(function _start: .text+0x0): relocation R_IA64_GPREL22 out of range: -16711336 is not in [-2097152, 2097151]; references section '.sdata'
# ERR-NEXT: ld.lld: error: a.o:(function _start: .text+0x1): relocation R_IA64_GPREL22 out of range: -16711320 is not in [-2097152, 2097151]; references section '.sbss'
# ERR-NEXT: ld.lld: error: a.o:(function _start: .text+0x2): relocation R_IA64_LTOFF22 out of range: -16711328 is not in [-2097152, 2097151]; references section '.data'
