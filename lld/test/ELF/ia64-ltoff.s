# REQUIRES: ia64
## The linkage table: the words that hold the addresses the code loads from
## gp. An address has one entry however many relocations and symbols ask for
## it, and the entry is relocated by the dynamic loader when the address is
## not known until then.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o b.so -o exe
# RUN: llvm-readelf -r -x .got exe | FileCheck %s --check-prefix=EXE
# RUN: llvm-nm exe | FileCheck %s --check-prefix=EXE-NM
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=EXE-DIS

# RUN: ld.lld -pie a.o b.so -o pie
# RUN: llvm-readelf -r -x .got pie | FileCheck %s --check-prefix=PIE

# RUN: ld.lld -shared a.o b.so -o so
# RUN: llvm-readelf -r -x .got so | FileCheck %s --check-prefix=SO

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        addl r14 = @ltoff(var), r1
        addl r15 = @ltoff(alias), r1
        addl r16 = @ltoff(sec1 + 8), r1 ;;
        addl r14 = @ltoff(sec2), r1
        addl r15 = @ltoffx(ext), r1 ;;
        ld8.mov r15 = [r15], ext
        movl r16 = @ltoff(ext + 16) ;;
        addl r14 = @ltoff(hid), r1
        nop.i 0 ;;
        br.ret.sptk.many b0 ;;
        .endp _start

        .data
sec1:
        data8 1
sec2:
        data8 2
        .global var, alias, hid
        .hidden hid
var:
alias:
        data8 3
hid:
        data8 4

#--- b.s
        .data
        .global ext
        .type ext, @object
        .size ext, 32
ext:
        .space 32

#--- end

# EXE: Relocation section '.rela.dyn' at offset 0x238 contains 2 entries:
# EXE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# EXE-NEXT: 40000000000203d0  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 0
# EXE-NEXT: 40000000000203d8  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 10
# EXE: Hex dump of section '.got':
# EXE-NEXT: 0x40000000000203c0 b0030200 00000040 a8030200 00000040 .......@.......@
# EXE-NEXT: 0x40000000000203d0 00000000 00000000 10000000 00000000 ................
# EXE-NEXT: 0x40000000000203e0 b8030200 00000040                   .......@

# EXE-NM: 40000000000102c0 d _DYNAMIC
# EXE-NM-NEXT: 40000000000203c0 d __gp
# EXE-NM-NEXT: 4000000000000270 T _start
# EXE-NM-NEXT: 40000000000203b0 D alias
# EXE-NM-NEXT: U ext
# EXE-NM-NEXT: 40000000000203b8 d hid
# EXE-NM-NEXT: 40000000000203a0 d sec1
# EXE-NM-NEXT: 40000000000203a8 d sec2
# EXE-NM-NEXT: 40000000000203b0 D var

# EXE-DIS: Disassembly of section .text:
# EXE-DIS: 4000000000000270 <_start>:
# EXE-DIS-NEXT: 4000000000000270:      	addl	r14 = 0, r1     // [MII]
# EXE-DIS-NEXT: 4000000000000276:      	addl	r15 = 0, r1
# EXE-DIS-NEXT: 400000000000027c:      	addl	r16 = 8, r1 ;;
# EXE-DIS-NEXT: 4000000000000280:      	addl	r14 = 8, r1     // [MII]
# EXE-DIS-NEXT: 4000000000000286:      	addl	r15 = 16, r1 ;;
# EXE-DIS-NEXT: 400000000000028c:      	nop.i	0
# EXE-DIS-NEXT: 4000000000000290:      	ld8	r15 = [r15]     // [MLX]
# EXE-DIS-NEXT: 4000000000000296:      	movl	r16 = 24 ;;
# EXE-DIS-NEXT: 40000000000002a0:      	addl	r14 = 32, r1    // [MII]
# EXE-DIS-NEXT: 40000000000002a6:      	nop.i	0 ;;
# EXE-DIS-NEXT: 40000000000002ac:      	nop.i	0
# EXE-DIS-NEXT: 40000000000002b0:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 40000000000002b6:      	nop.i	0
# EXE-DIS-NEXT: 40000000000002bc:      	br.ret.sptk.many	b0 ;;

# PIE: Relocation section '.rela.dyn' at offset 0x238 contains 5 entries:
# PIE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# PIE-NEXT: 0000000000020420  000000000000006f R_IA64_REL64LSB                   20410
# PIE-NEXT: 0000000000020428  000000000000006f R_IA64_REL64LSB                   20408
# PIE-NEXT: 0000000000020440  000000000000006f R_IA64_REL64LSB                   20418
# PIE-NEXT: 0000000000020430  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 0
# PIE-NEXT: 0000000000020438  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 10
# PIE: Hex dump of section '.got':
# PIE-NEXT: 0x00020420 10040200 00000000 08040200 00000000 ................
# PIE-NEXT: 0x00020430 00000000 00000000 10000000 00000000 ................
# PIE-NEXT: 0x00020440 18040200 00000000                   ........

# SO: Relocation section '.rela.dyn' at offset 0x2b8 contains 6 entries:
# SO-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# SO-NEXT: 00000000000204b0  000000000000006f R_IA64_REL64LSB                   20488
# SO-NEXT: 00000000000204c8  000000000000006f R_IA64_REL64LSB                   20498
# SO-NEXT: 00000000000204b8  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 0
# SO-NEXT: 00000000000204c0  0000000100000027 R_IA64_DIR64LSB        0000000000000000 ext + 10
# SO-NEXT: 00000000000204a0  0000000300000027 R_IA64_DIR64LSB        0000000000020490 var + 0
# SO-NEXT: 00000000000204a8  0000000400000027 R_IA64_DIR64LSB        0000000000020490 alias + 0
# SO: Hex dump of section '.got':
# SO-NEXT: 0x000204a0 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x000204b0 88040200 00000000 00000000 00000000 ................
# SO-NEXT: 0x000204c0 10000000 00000000 98040200 00000000 ................
