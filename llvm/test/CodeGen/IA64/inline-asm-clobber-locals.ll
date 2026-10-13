; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O0 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The prologue saves ar.pfs and the return link in the locals after the ones
; the body uses, and the register allocator leaves the last locals alone so
; that there are some. What the source wrote in assembly can still name one
; of them. The frame then reaches as far as that local, the outputs come
; after it, and the saves go in the locals below it that the body left
; alone: never in the eight registers that stand for the outputs, which the
; arguments of a call are written to.

declare i64 @ext(i64, i64)
declare i32 @setjmp(ptr) returns_twice

define i64 @last(i64 %a, i64 %b) {
; CHECK-LABEL: last:
; CHECK:       alloc r[[PFS:3[4-9]]] = ar.pfs, 2, 86, 2, 0
; CHECK:       mov r[[RP:3[4-9]]] = b0
; CHECK:       br.call.sptk.many b0 = ext
; CHECK:       br.call.sptk.many b0 = ext
; CHECK:       mov b0 = r[[RP]]
; CHECK-NEXT:  mov ar.pfs = r[[PFS]]
; CHECK-NEXT:  br.ret.sptk.many b0
  %r = call i64 @ext(i64 %a, i64 %b)
  call void asm sideeffect "nop 0", "~{r119}"()
  %s = call i64 @ext(i64 %b, i64 %a)
  %t = add i64 %r, %s
  ret i64 %t
}

; One local is left above the one that is named, and it is the first to be
; taken.
define i64 @next_to_last(i64 %a, i64 %b) {
; CHECK-LABEL: next_to_last:
; CHECK:       alloc r119 = ar.pfs, 2, 86, 2, 0
; CHECK:       mov r[[RP:3[4-9]]] = b0
; CHECK:       mov b0 = r[[RP]]
; CHECK-NEXT:  mov ar.pfs = r119
; CHECK-NEXT:  br.ret.sptk.many b0
  %r = call i64 @ext(i64 %a, i64 %b)
  call void asm sideeffect "nop 0", "~{r118}"()
  %s = call i64 @ext(i64 %b, i64 %a)
  %t = add i64 %r, %s
  ret i64 %t
}

; An operand of the assembly that is in the last local is as much a use of
; it as a clobber is.
define i64 @operand(i64 %a) {
; CHECK-LABEL: operand:
; CHECK:       alloc r[[PFS:3[3-9]]] = ar.pfs, 1, 87, 2, 0
; CHECK:       mov r[[RP:3[3-9]]] = b0
; CHECK:       mov r119 = r{{[0-9]+}}
; CHECK:       adds r{{[0-9]+}} = 1, r119
; CHECK:       mov b0 = r[[RP]]
; CHECK-NEXT:  mov ar.pfs = r[[PFS]]
; CHECK-NEXT:  br.ret.sptk.many b0
  %r = call i64 asm sideeffect "adds $0 = 1, $1", "=r,{r119}"(i64 %a)
  %s = call i64 @ext(i64 %r, i64 %r)
  ret i64 %s
}

; The locals that hold the saves are written once and are as the prologue
; left them when a call returns a second time, wherever in the frame they
; are: the verifier is run to see that the call is not taken to change them.
define i32 @twice(ptr %buf) {
; CHECK-LABEL: twice:
; CHECK:       alloc r[[PFS:3[3-9]]] = ar.pfs, 1, 87, 1, 0
; CHECK:       mov r[[RP:3[3-9]]] = b0
; CHECK:       br.call.sptk.many b0 = setjmp
; CHECK:       mov b0 = r[[RP]]
; CHECK-NEXT:  mov ar.pfs = r[[PFS]]
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{r119}"()
  %r = call i32 @setjmp(ptr %buf)
  ret i32 %r
}
