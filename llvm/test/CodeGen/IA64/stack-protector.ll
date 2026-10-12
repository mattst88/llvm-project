; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,LINUX
; RUN: llc -mtriple=ia64-unknown-elf -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,ELF

; The C library keeps the guard in the doubleword below the thread pointer and
; has no __stack_chk_guard, so the guard is read from there and never through
; the linkage table. Elsewhere it is the symbol.

declare void @g(ptr)

define void @f() sspreq {
; CHECK-LABEL: f:
; LINUX-NOT:   __stack_chk_guard
; LINUX:       adds [[G:r[0-9]+]] = -8, r13 ;;
; LINUX-NEXT:  ld8 r{{[0-9]+}} = [[[G]]]
; ELF:         __stack_chk_guard
; CHECK:       br.call.sptk.many b0 = g
; LINUX:       ld8 r{{[0-9]+}} = [[[G]]]
; LINUX-NOT:   __stack_chk_guard
; CHECK:       br.call.sptk.many b0 = __stack_chk_fail
  %buf = alloca [16 x i8]
  call void @g(ptr %buf)
  ret void
}
