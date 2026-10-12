# REQUIRES: ia64
## A function descriptor made in place where it cannot be written. When the
## dynamic loader has to fill it in, or to move it with the image, a read-only
## section cannot hold it unless -z notext lets the loader write there.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu addend.s -o addend.o

# RUN: not ld.lld -shared a.o -o /dev/null 2>&1 | FileCheck %s --check-prefixes=ERR,ERR-SO
# RUN: not ld.lld -pie a.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

## The function another module may define is the loader's to look up.
# ERR-SO:      error: relocation R_IA64_IPLTLSB against symbol 'global' needs a dynamic relocation in a read-only section
# ERR-SO-NEXT: >>> defined in a.o
# ERR-SO-NEXT: >>> referenced by a.o:(.rodata+0x0)
## The module's own has an address that moves with it.
# ERR:         error: relocation R_IA64_IPLTLSB against symbol 'hidden' needs a dynamic relocation in a read-only section
# ERR-NEXT:    >>> defined in a.o
# ERR-NEXT:    >>> referenced by a.o:(.rodata+0x10)
# ERR:         error: relocation R_IA64_IPLTLSB against local symbol needs a dynamic relocation in a read-only section
# ERR-NEXT:    >>> defined in a.o
# ERR-NEXT:    >>> referenced by a.o:(.rodata+0x20)
# ERR-NOT:     error:

## Where nothing moves there is nothing to write at run time.
# RUN: ld.lld a.o -o exe
# RUN: llvm-readelf -r -d exe | FileCheck %s --check-prefix=EXE
# EXE-NOT: TEXTREL
# EXE:     There are no relocations in this file.

# RUN: ld.lld -shared -z notext a.o -o so
# RUN: llvm-readelf -r -d -S so | FileCheck %s --check-prefix=SO
# SO:      .rodata PROGBITS [[#%x,RODATA:]]
# SO:      (FLAGS) TEXTREL
# SO:      (TEXTREL) 0x0
# SO:      Relocation section '.rela.dyn' {{.*}} contains 5 entries:
# SO:      {{0*}}[[#RODATA+16]] {{.*}} R_IA64_REL64LSB
# SO-NEXT: {{0*}}[[#RODATA+24]] {{.*}} R_IA64_REL64LSB
# SO-NEXT: {{0*}}[[#RODATA+32]] {{.*}} R_IA64_REL64LSB
# SO-NEXT: {{0*}}[[#RODATA+40]] {{.*}} R_IA64_REL64LSB
# SO-NEXT: {{0*}}[[#RODATA]] {{.*}} R_IA64_IPLTLSB {{.*}} global + 0

# RUN: ld.lld -pie -z notext a.o -o pie
# RUN: llvm-readelf -r -d pie | FileCheck %s --check-prefix=PIE
# PIE:     (TEXTREL) 0x0
# PIE:     Relocation section '.rela.dyn' {{.*}} contains 6 entries:
# PIE-NOT: R_IA64_IPLTLSB

## The loader takes no addend: it writes the two words of the function it
## finds.
# RUN: not ld.lld -shared addend.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ADDEND
# ADDEND:      error: relocation R_IA64_IPLTLSB has a non-zero addend against symbol 'global'
# ADDEND-NEXT: >>> defined in addend.o
# ADDEND-NEXT: >>> referenced by addend.o:(.data+0x0)

#--- a.s
        .text
        .global _start, global, hidden
        .hidden hidden
_start:
global:
        br.ret.sptk.many b0 ;;
hidden:
        br.ret.sptk.many b0 ;;
local:
        br.ret.sptk.many b0 ;;

        .section .rodata, "a", @progbits
        data16 @iplt(global)
        data16 @iplt(hidden)
        data16 @iplt(local)

#--- addend.s
        .text
        .global global
global:
        br.ret.sptk.many b0 ;;
        .data
        data16 @iplt(global + 16)
