# REQUIRES: ia64
## R_IA64_LDXMOV says that the instruction is the load of an address the
## add before it found in the linkage table, which the linker may make a
## move of. On anything that is not a load of eight bytes it is an error of
## whatever wrote it, and the instruction is not one to rewrite.

# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: not ld.lld %t.o -o /dev/null 2>&1 | FileCheck %s

# CHECK:     error: {{.*}}.o:(.text+0x20): R_IA64_LDXMOV is not on a load of 8 bytes
# CHECK:     error: {{.*}}.o:(.text+0x30): R_IA64_LDXMOV is not on a load of 8 bytes
# CHECK-NOT: error:

  .text
  .globl _start
_start:
  addl r14 = @ltoffx(var), r1
  ld8.mov r14 = [r14], var
  nop.i 0 ;;
  addl r15 = @ltoffx(var), r1
  .reloc ., LDXMOV, var
  ld4 r15 = [r15]
  .reloc ., R_IA64_LDXMOV, var
  add r16 = r15, r14 ;;

  .data
var:
  data8 0
