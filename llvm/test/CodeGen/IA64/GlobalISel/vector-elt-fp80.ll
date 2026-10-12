; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -stop-after=legalizer -simplify-mir -o - %s | FileCheck %s

; The elements of a vector of x86_fp80 are ten bytes apart in memory, and so
; the element that a variable index names is aligned to two bytes and not to
; the sixteen of the type. A load that claimed sixteen was split into loads
; that claimed eight, and those are the ones that trap.

; CHECK-LABEL: name: ext
; CHECK:       [[OFF:%[0-9]+]]:_(s64) = G_MUL
; CHECK:       [[P:%[0-9]+]]:_(p0) = G_PTR_ADD %{{[0-9]+}}, [[OFF]](s64)
; CHECK:       G_ZEXTLOAD [[P]](p0) :: (load (s16))
; CHECK-NOT:   from unknown-address{{.*}}align
; CHECK:       BR_RET
define x86_fp80 @ext(x86_fp80 %a, x86_fp80 %b, i64 %k) {
  %v0 = insertelement <2 x x86_fp80> poison, x86_fp80 %a, i32 0
  %v1 = insertelement <2 x x86_fp80> %v0, x86_fp80 %b, i32 1
  %e = extractelement <2 x x86_fp80> %v1, i64 %k
  ret x86_fp80 %e
}
