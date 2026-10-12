# REQUIRES: ia64
## A symbol that a linker script defines, or the linker itself, has no place
## until addresses are assigned, which is after the linkage table is laid
## out, and here the table moves them as it grows. Each has an entry of its
## own, and the entry holds where the symbol ends up.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: ld.lld -T a.lds a.o -o exe
# RUN: llvm-nm exe | FileCheck %s --check-prefix=NM
# RUN: llvm-readelf -x .got exe | FileCheck %s

# NM-DAG: 0000000000020030 D after
# NM-DAG: 0000000000020028 D before
# NM-DAG: 0000000000020030 D _edata
# NM-DAG: 0000000000001000 A fixed

# CHECK: Hex dump of section '.got':
# CHECK-NEXT: 0x00020000 28000200 00000000 30000200 00000000
# CHECK-NEXT: 0x00020010 00100000 00000000 30000200 00000000

#--- a.lds
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x20000;
  .got : { *(.got) }
  .data : { *(.data) before = .; *(.data.more) after = .; }
  _edata = .;
  fixed = 0x1000;
}

#--- a.s
        .text
        .global _start
        .proc _start
_start:
        addl r14 = @ltoff(before), r1
        addl r15 = @ltoff(after), r1
        addl r16 = @ltoff(fixed), r1 ;;
        addl r14 = @ltoff(_edata), r1
        nop.i 0
        br.ret.sptk.many b0 ;;
        .endp _start

        .data
        data8 1
        .section .data.more, "aw", @progbits
        data8 2
