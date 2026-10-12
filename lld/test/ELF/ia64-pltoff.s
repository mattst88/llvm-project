# REQUIRES: ia64
## @pltoff: the offset from gp of a descriptor to call a function through,
## which unlike @fptr need not be the one every module agrees on. For a
## function another module may define it is the entry of .IA_64.pltoff that
## the function's import stub uses. For one of this module's own it is a
## descriptor made here.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu addend.s -o addend.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o b.so -o exe
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=EXE-DIS
# RUN: llvm-readelf -S -r -s -x .opd -x .data exe | FileCheck %s --check-prefix=EXE

# RUN: ld.lld -pie a.o b.so -o pie
# RUN: llvm-readelf -S -r -x .data pie | FileCheck %s --check-prefix=PIE

## In a shared object the module's own exported function may be defined by
## another module too.
# RUN: ld.lld -shared a.o b.so -o so
# RUN: llvm-objdump -d --no-show-raw-insn so | FileCheck %s --check-prefix=SO-DIS
# RUN: llvm-readelf -S -r so | FileCheck %s --check-prefix=SO

# RUN: not ld.lld -shared addend.o b.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

## gp is the start of .IA_64.pltoff, whose first two entries are the dynamic
## loader's. extfn has the third. The descriptor of deffn is in .opd.
# EXE-DIS-LABEL: <_start>:
# EXE-DIS-NEXT:    addl r14 = 32, r1
# EXE-DIS-NEXT:    addl r15 = -[[#]], r1
# EXE-DIS-NEXT:    nop.i 0
# EXE-DIS-NEXT:    nop.m 0
# EXE-DIS-NEXT:    movl r16 = 32
# EXE-DIS-NEXT:    nop.m 0
# EXE-DIS-NEXT:    movl r17 = -[[#]]

# EXE:      .opd PROGBITS [[#%x,OPD:]] {{[0-9a-f]+}} 000010 00 A
# EXE:      .IA_64.pltoff PROGBITS [[#%x,GP:]] {{[0-9a-f]+}} 000030
# EXE:      [[#GP+32]] {{.*}} R_IA64_IPLTLSB 0000000000000000 extfn + 0
# EXE:      [[#%x,DEFFN:]] {{.*}} FUNC GLOBAL DEFAULT {{.*}} deffn
# EXE:      Hex dump of section '.opd':
# EXE-NEXT: 0x{{[0-9a-f]+}} a0020000 00000040 00040200 00000040
# EXE:      Hex dump of section '.data':
# EXE-NEXT: 0x{{[0-9a-f]+}} 20000000 00000000 50fefdff ffffffff

# PIE:      .opd PROGBITS [[#%x,OPD:]] {{[0-9a-f]+}} 000010 00 WA
# PIE:      .IA_64.pltoff PROGBITS [[#%x,GP:]] {{[0-9a-f]+}} 000030
# PIE:      [[#OPD]] {{.*}} R_IA64_REL64LSB 2c0
# PIE-NEXT: [[#OPD+8]] {{.*}} R_IA64_REL64LSB [[#GP]]
# PIE:      [[#GP+32]] {{.*}} R_IA64_IPLTLSB 0000000000000000 extfn + 0
# PIE:      Hex dump of section '.data':
# PIE-NEXT: 0x{{[0-9a-f]+}} 20000000 00000000 f0ffffff ffffffff

# SO-DIS-LABEL: <_start>:
# SO-DIS-NEXT:    addl r14 = 48, r1
# SO-DIS-NEXT:    addl r15 = 32, r1
# SO-DIS-NEXT:    nop.i 0
# SO-DIS-NEXT:    nop.m 0
# SO-DIS-NEXT:    movl r16 = 48
# SO-DIS-NEXT:    nop.m 0
# SO-DIS-NEXT:    movl r17 = 32

# SO-NOT:  .opd
# SO:      .IA_64.pltoff PROGBITS [[#%x,GP:]] {{[0-9a-f]+}} 000040
# SO:      [[#GP+32]] {{.*}} R_IA64_IPLTLSB {{[0-9a-f]+}} deffn + 0
# SO-NEXT: [[#GP+48]] {{.*}} R_IA64_IPLTLSB 0000000000000000 extfn + 0

# ERR: error: relocation R_IA64_PLTOFF22 has a non-zero addend against symbol 'extfn'

#--- a.s
        .text
        .global _start, deffn
        .proc _start
_start:
        addl r14 = @pltoff(extfn), r1
        addl r15 = @pltoff(deffn), r1 ;;
        movl r16 = @pltoff(extfn) ;;
        movl r17 = @pltoff(deffn) ;;
        br.call.sptk.many b0 = extfn ;;
        .endp _start
        .proc deffn
deffn:
        br.ret.sptk.many b0 ;;
        .endp deffn
        .data
        data8 @pltoff(extfn)
        data8 @pltoff(deffn)

#--- b.s
        .text
        .global extfn
        .proc extfn
extfn:
        br.ret.sptk.many b0 ;;
        .endp extfn

#--- addend.s
        .text
        addl r14 = @pltoff(extfn + 16), r1
