# REQUIRES: ia64
## @segrel and @secrel are offsets within the module: from the start of the
## segment, or of the section, that the symbol is in. They are to this
## module's own definition even of a symbol that another module may define
## instead at run time, and they never take a dynamic relocation.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu undef.s -o undef.o

# RUN: ld.lld a.o -o exe
# RUN: llvm-readelf -lW exe | FileCheck %s --check-prefix=EXE-HDR
# RUN: llvm-readelf -r -x .data exe | FileCheck %s --check-prefix=EXE
# RUN: ld.lld -shared a.o -o so
# RUN: llvm-readelf -lW --dyn-syms so | FileCheck %s --check-prefix=SO-HDR
# RUN: llvm-readelf -r -x .data so | FileCheck %s --check-prefix=SO

## The segment is the one the symbol is in, not the one the word is in: the
## function is 0x130 from the start of the text segment though the word that
## says so is in the data segment. GNU ld takes the segment of the word and
## writes 0 for what is below it.
# EXE-HDR:      LOAD 0x000000 0x4000000000000000
# EXE-HDR-NEXT: LOAD 0x000140 0x4000000000010140
# EXE:      There are no relocations in this file.
# EXE:      0x4000000000010140 30010000 00000000 08000000 00000000
# EXE-NEXT: 0x4000000000010150 10000000 00000000 08000000 00000000

# SO-HDR:      LOAD 0x000000 0x0000000000000000
# SO-HDR:      LOAD 0x000340 0x0000000000020340
# SO-HDR:      00000000000002a0 0 FUNC   GLOBAL DEFAULT {{.*}} gfn
# SO-HDR-NEXT: 0000000000020348 0 OBJECT GLOBAL DEFAULT {{.*}} gvar
# SO:      There are no relocations in this file.
# SO:      0x00020340 a0020000 00000000 08000000 00000000
# SO-NEXT: 0x00020350 10000000 00000000 08000000 00000000

## What another module defines is in no segment of this one.
# RUN: not ld.lld -shared undef.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
# ERR:      error: relocation R_IA64_SEGREL64LSB against symbol 'ext', which this module does not define
# ERR-NEXT: >>> defined in undef.o
# ERR-NEXT: >>> referenced by undef.o:(.data+0x0)
# ERR:      error: relocation R_IA64_SECREL32LSB against symbol 'ext', which this module does not define

#--- a.s
        .text
        .global _start, gfn
        .type gfn, @function
_start:
        br.ret.sptk.many b0 ;;
gfn:
        br.ret.sptk.many b0 ;;

        .data
        data8 @segrel(gfn)
        .global gvar
        .type gvar, @object
gvar:
        data8 @segrel(gvar)
        data8 @secrel(gfn)
        data8 @secrel(gvar)

#--- undef.s
        .data
        data8 @segrel(ext)
        data4 @secrel(ext)
