# REQUIRES: ia64
## The segment of the unwind table when a linker script lays out the program
## headers. The unwinder takes the first PT_IA_64_UNWIND it finds, so there is
## one: the script's, if it has one, and otherwise one made for the table.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o

# RUN: ld.lld -T with.t a.o -o with
# RUN: llvm-readelf -lW with | FileCheck %s
# RUN: ld.lld -T without.t a.o -o without
# RUN: llvm-readelf -lW without | FileCheck %s

# CHECK:      Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# CHECK-NEXT: LOAD           0x000000 0x0000000000000000 0x0000000000000000 {{.*}} R E 0x10000
# CHECK-NEXT: IA_64_UNWIND   0x{{[0-9a-f]+}} 0x{{[0-9a-f]+}} 0x{{[0-9a-f]+}} 0x000018 0x000018 R   0x8
# CHECK-NOT:  IA_64_UNWIND
# CHECK:      Section to Segment mapping:
# CHECK:      00     .text .IA_64.unwind_info .IA_64.unwind
# CHECK-NEXT: 01     .IA_64.unwind
# CHECK-NOT:  .IA_64.unwind

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        .prologue
        .save ar.pfs, r32
        alloc r32 = ar.pfs, 0, 1, 0, 0
        .body
        br.ret.sptk.many b0 ;;
        .endp _start

#--- with.t
PHDRS {
  text PT_LOAD FILEHDR PHDRS;
  unwind 0x70000001;
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .text : { *(.text*) } :text
  .IA_64.unwind_info : { *(.IA_64.unwind_info*) }
  .IA_64.unwind : { *(.IA_64.unwind*) } :text :unwind
}

#--- without.t
PHDRS {
  text PT_LOAD FILEHDR PHDRS;
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .text : { *(.text*) } :text
  .IA_64.unwind_info : { *(.IA_64.unwind_info*) }
  .IA_64.unwind : { *(.IA_64.unwind*) }
}
