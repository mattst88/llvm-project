# REQUIRES: ia64
## The address of a function is the address of its descriptor, and every module
## has to see the same one. A function that cannot be seen from another module
## gets a descriptor here; for any other the dynamic loader finds or makes it.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu ro.s -o ro.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o b.o -o static
# RUN: llvm-readelf -r -x .opd -x .got -x .data static | FileCheck %s --check-prefix=STATIC
# RUN: llvm-nm static | FileCheck %s --check-prefix=STATIC-NM

# RUN: ld.lld a.o b.so -o exe
# RUN: llvm-readelf -r -x .opd -x .got -x .data exe | FileCheck %s --check-prefix=EXE

# RUN: ld.lld -pie a.o b.so -o pie
# RUN: llvm-readelf -r -x .opd -x .got -x .data pie | FileCheck %s --check-prefix=PIE

# RUN: ld.lld -shared a.o b.so -o so
# RUN: llvm-readelf -r -x .opd -x .got -x .data so | FileCheck %s --check-prefix=SO

## A descriptor the dynamic loader makes is a relocation of the word that
## holds its address, so the word has to be writable.
# RUN: not ld.lld -shared ro.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RO
# RUN: ld.lld -shared -z notext ro.o -o ro.so
# RUN: llvm-readelf -r -d ro.so | FileCheck %s --check-prefix=NOTEXT

# RO: error: relocation R_IA64_FPTR64LSB against symbol 'func' needs a dynamic relocation in a read-only section
# RO-NEXT: >>> defined in ro.o
# RO-NEXT: >>> referenced by ro.o:(.rodata+0x0)

## The loader makes the descriptor for a word of 32 bits as well. What it
## cannot do is add to one the address this module is loaded at.
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu w32.s -o w32.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu l32.s -o l32.o
# RUN: ld.lld -shared w32.o -o w32.so
# RUN: llvm-readelf -r w32.so | FileCheck %s --check-prefix=W32
# RUN: ld.lld -pie --export-dynamic w32.o -o w32
# RUN: llvm-readelf -r w32 | FileCheck %s --check-prefix=W32
# RUN: not ld.lld -shared l32.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=L32

# W32:      Relocation section '.rela.dyn' at offset {{.*}} contains 1 entries:
# W32:      {{0*}}[[#%x,]] {{.*}} R_IA64_FPTR32LSB {{0*}}[[#%x,]] func + 0

# L32:      error: relocation R_IA64_FPTR64I cannot be used against symbol 'func'; recompile with -fPIC
# L32:      error: relocation R_IA64_FPTR32LSB cannot be used against symbol 'hid'; recompile with -fPIC
# L32-NEXT: >>> defined in l32.o
# L32-NEXT: >>> referenced by l32.o:(.data+0x0)

#--- a.s
        .text
        .global _start, func
        .weak undef
        .hidden hid
        .proc _start
_start:
        addl r14 = @ltoff(@fptr(func)), r1
        addl r15 = @ltoff(@fptr(hid)), r1
        addl r16 = @ltoff(@fptr(local)), r1 ;;
        addl r14 = @ltoff(@fptr(ext)), r1
        addl r15 = @ltoff(@fptr(undef)), r1 ;;
        br.ret.sptk.many b0 ;;
        .endp _start
        .proc func
func:
        br.ret.sptk.many b0 ;;
        .endp func
        .proc hid
hid:
        br.ret.sptk.many b0 ;;
        .endp hid
        .proc local
local:
        br.ret.sptk.many b0 ;;
        .endp local

        .data
        data8 @fptr(func)
        data8 @fptr(hid)
        data8 @fptr(local)
        data8 @fptr(ext)
        data8 @fptr(undef)

#--- b.s
        .text
        .global ext
        .proc ext
ext:
        br.ret.sptk.many b0 ;;
        .endp ext

#--- ro.s
        .text
        .global func
        .proc func
func:
        br.ret.sptk.many b0 ;;
        .endp func
        .section .rodata, "a", @progbits
        data8 @fptr(func)

#--- w32.s
        .text
        .global func
        .proc func
func:
        br.ret.sptk.many b0 ;;
        .endp func
        .data
        data4 @fptr(func)

#--- l32.s
        .text
        .global func
        .hidden hid
        .proc func
func:
        movl r14 = @fptr(func) ;;
        .endp func
        .proc hid
hid:
        br.ret.sptk.many b0 ;;
        .endp hid
        .data
        data4 @fptr(hid)

#--- end

# STATIC: There are no relocations in this file.
# STATIC: Hex dump of section '.opd':
# STATIC-NEXT: 0x4000000000000120 90010000 00000040 f8010100 00000040 .......@.......@
# STATIC-NEXT: 0x4000000000000130 a0010000 00000040 f8010100 00000040 .......@.......@
# STATIC-NEXT: 0x4000000000000140 b0010000 00000040 f8010100 00000040 .......@.......@
# STATIC-NEXT: 0x4000000000000150 c0010000 00000040 f8010100 00000040 .......@.......@
# STATIC: Hex dump of section '.data':
# STATIC-NEXT: 0x40000000000101d0 20010000 00000040 30010000 00000040  ......@0......@
# STATIC-NEXT: 0x40000000000101e0 40010000 00000040 50010000 00000040 @......@P......@
# STATIC-NEXT: 0x40000000000101f0 00000000 00000000                   ........
# STATIC: Hex dump of section '.got':
# STATIC-NEXT: 0x40000000000101f8 20010000 00000040 30010000 00000040  ......@0......@
# STATIC-NEXT: 0x4000000000010208 40010000 00000040 50010000 00000040 @......@P......@
# STATIC-NEXT: 0x4000000000010218 00000000 00000000                   ........

# STATIC-NM: 40000000000101f8 d __gp
# STATIC-NM-NEXT: 4000000000000160 T _start
# STATIC-NM-NEXT: 40000000000001c0 T ext
# STATIC-NM-NEXT: 4000000000000190 T func
# STATIC-NM-NEXT: 40000000000001a0 t hid
# STATIC-NM-NEXT: 40000000000001b0 t local
# STATIC-NM-NEXT: w undef

# EXE: Relocation section '.rela.dyn' at offset 0x260 contains 4 entries:
# EXE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# EXE-NEXT: 4000000000020450  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# EXE-NEXT: 4000000000020478  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# EXE-NEXT: 4000000000020448  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# EXE-NEXT: 4000000000020470  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# EXE: Hex dump of section '.opd':
# EXE-NEXT: 0x40000000000002c0 20030000 00000040 58040200 00000040  ......@X......@
# EXE-NEXT: 0x40000000000002d0 30030000 00000040 58040200 00000040 0......@X......@
# EXE-NEXT: 0x40000000000002e0 40030000 00000040 58040200 00000040 @......@X......@
# EXE: Hex dump of section '.data':
# EXE-NEXT: 0x4000000000020430 c0020000 00000040 d0020000 00000040 .......@.......@
# EXE-NEXT: 0x4000000000020440 e0020000 00000040 00000000 00000000 .......@........
# EXE-NEXT: 0x4000000000020450 00000000 00000000                   ........
# EXE: Hex dump of section '.got':
# EXE-NEXT: 0x4000000000020458 c0020000 00000040 d0020000 00000040 .......@.......@
# EXE-NEXT: 0x4000000000020468 e0020000 00000040 00000000 00000000 .......@........
# EXE-NEXT: 0x4000000000020478 00000000 00000000                   ........

# PIE: Relocation section '.rela.dyn' at offset 0x260 contains 16 entries:
# PIE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# PIE-NEXT: 0000000000020540  000000000000006f R_IA64_REL64LSB                   20570
# PIE-NEXT: 0000000000020548  000000000000006f R_IA64_REL64LSB                   20580
# PIE-NEXT: 0000000000020550  000000000000006f R_IA64_REL64LSB                   20590
# PIE-NEXT: 0000000000020570  000000000000006f R_IA64_REL64LSB                   410
# PIE-NEXT: 0000000000020578  000000000000006f R_IA64_REL64LSB                   205a0
# PIE-NEXT: 0000000000020580  000000000000006f R_IA64_REL64LSB                   420
# PIE-NEXT: 0000000000020588  000000000000006f R_IA64_REL64LSB                   205a0
# PIE-NEXT: 0000000000020590  000000000000006f R_IA64_REL64LSB                   430
# PIE-NEXT: 0000000000020598  000000000000006f R_IA64_REL64LSB                   205a0
# PIE-NEXT: 00000000000205a0  000000000000006f R_IA64_REL64LSB                   20570
# PIE-NEXT: 00000000000205a8  000000000000006f R_IA64_REL64LSB                   20580
# PIE-NEXT: 00000000000205b0  000000000000006f R_IA64_REL64LSB                   20590
# PIE-NEXT: 0000000000020560  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# PIE-NEXT: 00000000000205c0  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# PIE-NEXT: 0000000000020558  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# PIE-NEXT: 00000000000205b8  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# PIE: Hex dump of section '.data':
# PIE-NEXT: 0x00020540 70050200 00000000 80050200 00000000 p...............
# PIE-NEXT: 0x00020550 90050200 00000000 00000000 00000000 ................
# PIE-NEXT: 0x00020560 00000000 00000000                   ........
# PIE: Hex dump of section '.opd':
# PIE-NEXT: 0x00020570 10040000 00000000 a0050200 00000000 ................
# PIE-NEXT: 0x00020580 20040000 00000000 a0050200 00000000  ...............
# PIE-NEXT: 0x00020590 30040000 00000000 a0050200 00000000 0...............
# PIE: Hex dump of section '.got':
# PIE-NEXT: 0x000205a0 70050200 00000000 80050200 00000000 p...............
# PIE-NEXT: 0x000205b0 90050200 00000000 00000000 00000000 ................
# PIE-NEXT: 0x000205c0 00000000 00000000                   ........

# SO: Relocation section '.rela.dyn' at offset 0x2b0 contains 14 entries:
# SO-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# SO-NEXT: 0000000000020548  000000000000006f R_IA64_REL64LSB                   20570
# SO-NEXT: 0000000000020550  000000000000006f R_IA64_REL64LSB                   20580
# SO-NEXT: 0000000000020570  000000000000006f R_IA64_REL64LSB                   440
# SO-NEXT: 0000000000020578  000000000000006f R_IA64_REL64LSB                   20590
# SO-NEXT: 0000000000020580  000000000000006f R_IA64_REL64LSB                   450
# SO-NEXT: 0000000000020588  000000000000006f R_IA64_REL64LSB                   20590
# SO-NEXT: 0000000000020598  000000000000006f R_IA64_REL64LSB                   20570
# SO-NEXT: 00000000000205a0  000000000000006f R_IA64_REL64LSB                   20580
# SO-NEXT: 0000000000020560  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# SO-NEXT: 00000000000205b0  0000000100000047 R_IA64_FPTR64LSB       0000000000000000 undef + 0
# SO-NEXT: 0000000000020558  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# SO-NEXT: 00000000000205a8  0000000200000047 R_IA64_FPTR64LSB       0000000000000000 ext + 0
# SO-NEXT: 0000000000020540  0000000400000047 R_IA64_FPTR64LSB       0000000000000430 func + 0
# SO-NEXT: 0000000000020590  0000000400000047 R_IA64_FPTR64LSB       0000000000000430 func + 0
# SO: Hex dump of section '.data':
# SO-NEXT: 0x00020540 00000000 00000000 70050200 00000000 ........p.......
# SO-NEXT: 0x00020550 80050200 00000000 00000000 00000000 ................
# SO-NEXT: 0x00020560 00000000 00000000                   ........
# SO: Hex dump of section '.opd':
# SO-NEXT: 0x00020570 40040000 00000000 90050200 00000000 @...............
# SO-NEXT: 0x00020580 50040000 00000000 90050200 00000000 P...............
# SO: Hex dump of section '.got':
# SO-NEXT: 0x00020590 00000000 00000000 70050200 00000000 ........p.......
# SO-NEXT: 0x000205a0 80050200 00000000 00000000 00000000 ................
# SO-NEXT: 0x000205b0 00000000 00000000                   ........

# NOTEXT: Dynamic section at offset 0x270 contains 14 entries:
# NOTEXT-NEXT: Tag                Type                Name/Value
# NOTEXT-NEXT: 0x000000000000001e (FLAGS)             TEXTREL
# NOTEXT-NEXT: 0x0000000000000007 (RELA)              0x238
# NOTEXT-NEXT: 0x0000000000000008 (RELASZ)            24 (bytes)
# NOTEXT-NEXT: 0x0000000000000009 (RELAENT)           24 (bytes)
# NOTEXT-NEXT: 0x0000000000000003 (PLTGOT)            0x20350
# NOTEXT-NEXT: 0x0000000070000000 (IA_64_PLT_RESERVE) 0x20350
# NOTEXT-NEXT: 0x0000000000000006 (SYMTAB)            0x1c8
# NOTEXT-NEXT: 0x000000000000000b (SYMENT)            24 (bytes)
# NOTEXT-NEXT: 0x0000000000000005 (STRTAB)            0x230
# NOTEXT-NEXT: 0x000000000000000a (STRSZ)             6 (bytes)
# NOTEXT-NEXT: 0x0000000000000016 (TEXTREL)           0x0
# NOTEXT-NEXT: 0x000000006ffffef5 (GNU_HASH)          0x1f8
# NOTEXT-NEXT: 0x0000000000000004 (HASH)              0x218
# NOTEXT-NEXT: 0x0000000000000000 (NULL)              0x0
# NOTEXT: Relocation section '.rela.dyn' at offset 0x238 contains 1 entries:
# NOTEXT-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# NOTEXT-NEXT: 0000000000000250  0000000100000047 R_IA64_FPTR64LSB       0000000000000260 func + 0
