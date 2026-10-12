# REQUIRES: ia64
## Thread-local storage. The thread pointer is r13, and the block of the
## executable begins 16 bytes above it, further when the segment is aligned to
## more. The other models load a module, an offset in it, or an offset from
## the thread pointer from the linkage table, and the dynamic loader fills
## those in when they are not known here.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu le.s -o le.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o le.o b.so -o exe
# RUN: llvm-readelf -r -l -x .got exe | FileCheck %s --check-prefix=EXE
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=EXE-DIS

# RUN: ld.lld -pie a.o le.o b.so -o pie
# RUN: llvm-readelf -r -x .got pie | FileCheck %s --check-prefix=PIE

# RUN: ld.lld -shared a.o b.so -o so
# RUN: llvm-readelf -r -d -x .got so | FileCheck %s --check-prefix=SO

## The offset from the thread pointer of a variable of a shared object is not
## known until it is loaded.
# RUN: not ld.lld -shared a.o le.o b.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=LE

# LE: error: relocation R_IA64_TPREL14 against hid cannot be used with -shared
# LE: error: relocation R_IA64_TPREL22 against glob cannot be used with -shared
# LE: error: relocation R_IA64_TPREL64I against hid cannot be used with -shared
# LE: error: relocation R_IA64_TPREL64LSB against glob cannot be used with -shared

#--- a.s
        .text
        .global _start
        .proc _start
_start:
## Initial exec.
        addl r14 = @ltoff(@tprel(tl)), r1
        addl r15 = @ltoff(@tprel(glob)), r1
        addl r16 = @ltoff(@tprel(ext)), r1 ;;
## General dynamic.
        addl r14 = @ltoff(@dtpmod(glob)), r1
        addl r15 = @ltoff(@dtprel(glob)), r1
        nop.i 0 ;;
        addl r14 = @ltoff(@dtpmod(ext)), r1
        addl r15 = @ltoff(@dtprel(ext)), r1
        nop.i 0 ;;
## Local dynamic.
        addl r14 = @ltoff(@dtpmod(tl)), r1
        adds r15 = @dtprel(tl), r8
        addl r16 = @dtprel(tl + 4), r2 ;;
        movl r17 = @dtprel(hid) ;;
        br.ret.sptk.many b0 ;;
        .endp _start

        .section .tdata, "awT", @progbits
        .global glob, hid
        .hidden hid
tl:
        data4 1
        data4 2
glob:
        data8 3
hid:
        data8 4

        .data
        data8 @dtprel(glob)
        data8 @dtpmod(glob)
        data8 @dtpmod(tl)
        data8 @dtpmod(ext)

#--- b.s
        .section .tbss, "awT", @nobits
        .global ext
        .type ext, @object
        .size ext, 8
ext:
        .space 8

#--- le.s
## Local exec.
        .text
        .global le
        .proc le
le:
        adds r14 = @tprel(hid), r13
        addl r15 = @tprel(glob), r2
        nop.i 0 ;;
        movl r16 = @tprel(hid + 4) ;;
        br.ret.sptk.many b0 ;;
        .endp le

        .data
        data8 @tprel(glob)

#--- end

# EXE: Elf file type is EXEC (Executable file)
# EXE-NEXT: Entry point 0x40000000000002d0
# EXE-NEXT: There are 8 program headers, starting at offset 64
# EXE: Program Headers:
# EXE-NEXT: Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# EXE-NEXT: PHDR           0x000040 0x4000000000000040 0x4000000000000040 0x0001c0 0x0001c0 R   0x8
# EXE-NEXT: LOAD           0x000000 0x4000000000000000 0x4000000000000000 0x000360 0x000360 R E 0x10000
# EXE-NEXT: LOAD           0x000360 0x4000000000010360 0x4000000000010360 0x0000f8 0x00fca0 RW  0x10000
# EXE-NEXT: LOAD           0x000458 0x4000000000020458 0x4000000000020458 0x000098 0x000098 RW  0x10000
# EXE-NEXT: TLS            0x000360 0x4000000000010360 0x4000000000010360 0x000018 0x000018 R   0x8
# EXE-NEXT: DYNAMIC        0x000378 0x4000000000010378 0x4000000000010378 0x0000e0 0x0000e0 RW  0x8
# EXE-NEXT: GNU_RELRO      0x000360 0x4000000000010360 0x4000000000010360 0x0000f8 0x00fca0 R   0x1
# EXE-NEXT: GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0
# EXE: Section to Segment mapping:
# EXE-NEXT: Segment Sections...
# EXE-NEXT: 00
# EXE-NEXT: 01     .dynsym .gnu.hash .hash .dynstr .rela.dyn .text
# EXE-NEXT: 02     .tdata .dynamic .relro_padding
# EXE-NEXT: 03     .data .got .IA_64.pltoff
# EXE-NEXT: 04     .tdata
# EXE-NEXT: 05     .dynamic
# EXE-NEXT: 06     .tdata .dynamic .relro_padding
# EXE-NEXT: 07
# EXE-NEXT: None   .comment .symtab .shstrtab .strtab
# EXE: Relocation section '.rela.dyn' at offset 0x270 contains 4 entries:
# EXE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# EXE-NEXT: 4000000000020470  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# EXE-NEXT: 40000000000204a8  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# EXE-NEXT: 40000000000204b0  00000001000000b7 R_IA64_DTPREL64LSB     0000000000000000 ext + 0
# EXE-NEXT: 40000000000204b8  0000000100000097 R_IA64_TPREL64LSB      0000000000000000 ext + 0
# EXE: Hex dump of section '.got':
# EXE-NEXT: 0x4000000000020480 01000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x4000000000020490 01000000 00000000 08000000 00000000 ................
# EXE-NEXT: 0x40000000000204a0 18000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x40000000000204b0 00000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x40000000000204c0 10000000 00000000                   ........

# EXE-DIS: Disassembly of section .text:
# EXE-DIS: 40000000000002d0 <_start>:
# EXE-DIS-NEXT: 40000000000002d0:      	addl	r14 = 64, r1    // [MII]
# EXE-DIS-NEXT: 40000000000002d6:      	addl	r15 = 32, r1
# EXE-DIS-NEXT: 40000000000002dc:      	addl	r16 = 56, r1 ;;
# EXE-DIS-NEXT: 40000000000002e0:      	addl	r14 = 0, r1     // [MII]
# EXE-DIS-NEXT: 40000000000002e6:      	addl	r15 = 24, r1
# EXE-DIS-NEXT: 40000000000002ec:      	nop.i	0 ;;
# EXE-DIS-NEXT: 40000000000002f0:      	addl	r14 = 40, r1    // [MII]
# EXE-DIS-NEXT: 40000000000002f6:      	addl	r15 = 48, r1
# EXE-DIS-NEXT: 40000000000002fc:      	nop.i	0 ;;
# EXE-DIS-NEXT: 4000000000000300:      	addl	r14 = 0, r1     // [MII]
# EXE-DIS-NEXT: 4000000000000306:      	mov	r15 = r8
# EXE-DIS-NEXT: 400000000000030c:      	addl	r16 = 4, r2 ;;
# EXE-DIS-NEXT: 4000000000000310:      	nop.m	0               // [MLX]
# EXE-DIS-NEXT: 4000000000000316:      	movl	r17 = 16 ;;
# EXE-DIS-NEXT: 4000000000000320:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 4000000000000326:      	nop.i	0
# EXE-DIS-NEXT: 400000000000032c:      	br.ret.sptk.many	b0 ;;
# EXE-DIS: 4000000000000330 <le>:
# EXE-DIS-NEXT: 4000000000000330:      	adds	r14 = 32, r13   // [MII]
# EXE-DIS-NEXT: 4000000000000336:      	addl	r15 = 24, r2
# EXE-DIS-NEXT: 400000000000033c:      	nop.i	0 ;;
# EXE-DIS-NEXT: 4000000000000340:      	nop.m	0               // [MLX]
# EXE-DIS-NEXT: 4000000000000346:      	movl	r16 = 36 ;;
# EXE-DIS-NEXT: 4000000000000350:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 4000000000000356:      	nop.i	0
# EXE-DIS-NEXT: 400000000000035c:      	br.ret.sptk.many	b0 ;;

# PIE: Relocation section '.rela.dyn' at offset 0x270 contains 4 entries:
# PIE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# PIE-NEXT: 0000000000020480  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# PIE-NEXT: 00000000000204b8  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# PIE-NEXT: 00000000000204c0  00000001000000b7 R_IA64_DTPREL64LSB     0000000000000000 ext + 0
# PIE-NEXT: 00000000000204c8  0000000100000097 R_IA64_TPREL64LSB      0000000000000000 ext + 0
# PIE: Hex dump of section '.got':
# PIE-NEXT: 0x00020490 01000000 00000000 00000000 00000000 ................
# PIE-NEXT: 0x000204a0 01000000 00000000 08000000 00000000 ................
# PIE-NEXT: 0x000204b0 18000000 00000000 00000000 00000000 ................
# PIE-NEXT: 0x000204c0 00000000 00000000 00000000 00000000 ................
# PIE-NEXT: 0x000204d0 10000000 00000000                   ........

# SO: Dynamic section at offset 0x448 contains 14 entries:
# SO-NEXT: Tag                Type                Name/Value
# SO-NEXT: 0x0000000000000001 (NEEDED)            Shared library: [b.so]
# SO-NEXT: 0x000000000000001e (FLAGS)             STATIC_TLS
# SO-NEXT: 0x0000000000000007 (RELA)              0x2c8
# SO-NEXT: 0x0000000000000008 (RELASZ)            264 (bytes)
# SO-NEXT: 0x0000000000000009 (RELAENT)           24 (bytes)
# SO-NEXT: 0x0000000000000003 (PLTGOT)            0x20548
# SO-NEXT: 0x0000000070000000 (IA_64_PLT_RESERVE) 0x20590
# SO-NEXT: 0x0000000000000006 (SYMTAB)            0x200
# SO-NEXT: 0x000000000000000b (SYMENT)            24 (bytes)
# SO-NEXT: 0x0000000000000005 (STRTAB)            0x2ac
# SO-NEXT: 0x000000000000000a (STRSZ)             22 (bytes)
# SO-NEXT: 0x000000006ffffef5 (GNU_HASH)          0x260
# SO-NEXT: 0x0000000000000004 (HASH)              0x284
# SO-NEXT: 0x0000000000000000 (NULL)              0x0
# SO: Relocation section '.rela.dyn' at offset 0x2c8 contains 11 entries:
# SO-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# SO-NEXT: 0000000000020538  00000000000000a7 R_IA64_DTPMOD64LSB                0
# SO-NEXT: 0000000000020548  00000000000000a7 R_IA64_DTPMOD64LSB                0
# SO-NEXT: 0000000000020588  0000000000000097 R_IA64_TPREL64LSB                 0
# SO-NEXT: 0000000000020540  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# SO-NEXT: 0000000000020570  00000001000000a7 R_IA64_DTPMOD64LSB     0000000000000000 ext + 0
# SO-NEXT: 0000000000020578  00000001000000b7 R_IA64_DTPREL64LSB     0000000000000000 ext + 0
# SO-NEXT: 0000000000020580  0000000100000097 R_IA64_TPREL64LSB      0000000000000000 ext + 0
# SO-NEXT: 0000000000020530  00000003000000a7 R_IA64_DTPMOD64LSB     0000000000000008 glob + 0
# SO-NEXT: 0000000000020558  00000003000000a7 R_IA64_DTPMOD64LSB     0000000000000008 glob + 0
# SO-NEXT: 0000000000020560  00000003000000b7 R_IA64_DTPREL64LSB     0000000000000008 glob + 0
# SO-NEXT: 0000000000020568  0000000300000097 R_IA64_TPREL64LSB      0000000000000008 glob + 0
# SO: Hex dump of section '.got':
# SO-NEXT: 0x00020548 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x00020558 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x00020568 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x00020578 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x00020588 00000000 00000000                   ........
