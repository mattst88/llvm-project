; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; b1 to b5, p1 to p5, p16 to p63 and ar.lc are the caller's. A function that
; changes one keeps what was in it in a local and tells the unwinder which.
; Only what the source wrote in assembly changes any of them. A branch
; register is not described with .save.b, which puts it in the order of the
; registers spilled to memory as well: the unwinder of libgcc gives up on a
; register that is there and has no place in memory.

define void @branch() {
; CHECK-LABEL: branch:
; CHECK:       alloc r32 = ar.pfs, 0, 3, 0, 0
; CHECK-NOT:   .save.b
; CHECK:       .spillreg b1, r33
; CHECK-NEXT:  mov r33 = b1
; CHECK-NEXT:  .spillreg b5, r34
; CHECK-NEXT:  mov r34 = b5
; CHECK:       .body
; CHECK:       mov b1 = r33
; CHECK-NEXT:  mov b5 = r34
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{b1},~{b5}"()
  ret void
}

; The scratch branch registers are not.
define void @scratch() {
; CHECK-LABEL: scratch:
; CHECK-NOT:   alloc
; CHECK-NOT:   .save
; CHECK:       br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{b6},~{b7},~{p6},~{p15}"()
  ret void
}

; The predicates are saved and put back together, and only the ones that are
; the caller's are put back.
define void @predicate() {
; CHECK-LABEL: predicate:
; CHECK:       alloc r32 = ar.pfs, 0, 2, 0, 0
; CHECK:       .save pr, r33
; CHECK-NEXT:  mov r33 = pr
; CHECK:       .body
; CHECK:       mov pr = r33, 65598
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{p1}"()
  ret void
}

define void @rotating() {
; CHECK-LABEL: rotating:
; CHECK:       .save pr, r33
; CHECK-NEXT:  mov r33 = pr
; CHECK:       mov pr = r33, 65598
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{p16},~{p63}"()
  ret void
}

define void @count() {
; CHECK-LABEL: count:
; CHECK:       alloc r32 = ar.pfs, 0, 2, 0, 0
; CHECK:       .save ar.lc, r33
; CHECK-NEXT:  mov r33 = ar.lc
; CHECK:       .body
; CHECK:       mov ar.lc = r33
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{ar.lc}"()
  ret void
}

; They are saved before the function goes on, and are put back before it is.
declare void @g()
define void @all() {
; CHECK-LABEL: all:
; CHECK:       alloc r33 = ar.pfs, 0, 6, 0, 0
; CHECK:       mov r35 = pr
; CHECK:       .spillreg b2, r37
; CHECK-NEXT:  mov r37 = b2
; CHECK:       mov r36 = ar.lc
; CHECK:       mov r34 = b0
; CHECK:       .body
; CHECK:       br.call.sptk.many b0 = g
; CHECK:  mov ar.lc = r36
; CHECK-NEXT:  mov b2 = r37
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  mov pr = r35, 65598
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{b2},~{p2},~{ar.lc}"()
  call void @g()
  ret void
}
