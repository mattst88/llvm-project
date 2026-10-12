; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s \
; RUN:   | llvm-mc -triple=ia64-unknown-linux-gnu -filetype=obj -o %t.o
; RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=OBJ

;; GNU as places stops itself unless it is told that the ones in the file are
;; all there are, and drops the ones that it is given with a warning for
;; each. The file says so before anything that might have a stop in it, the
;; assembly of the module included.

; CHECK:      .explicit
; CHECK-NOT:  .explicit
; CHECK:      mov r14 = r15
; CHECK-NEXT: ;;
; CHECK:      f:
; CHECK:      add r8 = r32, r33
; CHECK-NEXT: br.ret.sptk.many b0 ;;

; OBJ:      mov r14 = r15
; OBJ-NEXT: nop.i 0
; OBJ-NEXT: nop.i 0 ;;
; OBJ:      <f>:
; OBJ:      add r8 = r32, r33
; OBJ:      br.ret.sptk.many b0 ;;

module asm "mov r14 = r15 ;;"

define i64 @f(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}
