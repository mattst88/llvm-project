# REQUIRES: ia64

# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: llvm-readobj --file-headers %t | FileCheck %s
# RUN: ld.lld -m elf64_ia64 %t.o -o %t
# RUN: llvm-readobj --file-headers %t | FileCheck %s
# RUN: echo 'OUTPUT_FORMAT(elf64-ia64-little)' > %t.script
# RUN: ld.lld %t.script %t.o -o %t
# RUN: llvm-readobj --file-headers %t | FileCheck %s

# CHECK:      ElfHeader {
# CHECK-NEXT:   Ident {
# CHECK-NEXT:     Magic: (7F 45 4C 46)
# CHECK-NEXT:     Class: 64-bit (0x2)
# CHECK-NEXT:     DataEncoding: LittleEndian (0x1)
# CHECK-NEXT:     FileVersion: 1
# CHECK-NEXT:     OS/ABI: SystemV (0x0)
# CHECK-NEXT:     ABIVersion: 0
# CHECK-NEXT:     Unused: (00 00 00 00 00 00 00)
# CHECK-NEXT:   }
# CHECK-NEXT:   Type: Executable (0x2)
# CHECK-NEXT:   Machine: EM_IA_64 (0x32)
# CHECK-NEXT:   Version: 1
# CHECK-NEXT:   Entry:
# CHECK-NEXT:   ProgramHeaderOffset: 0x40
# CHECK-NEXT:   SectionHeaderOffset:
# CHECK-NEXT:   Flags [ (0x10)
# CHECK-NEXT:     EF_IA_64_ABI64 (0x10)
# CHECK-NEXT:   ]

        .global _start
_start:
        br.ret.sptk.many b0 ;;
