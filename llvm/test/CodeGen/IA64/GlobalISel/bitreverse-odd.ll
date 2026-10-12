; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -stop-after=legalizer -simplify-mir -o - %s | FileCheck %s

; The lowering reverses the bytes and then the bits of each byte, and that
; is the reversal only where the width is a whole number of bytes. Seventeen
; bits are reversed as twenty-four, and the seven that were added are shifted
; out of the bottom.

; CHECK-LABEL: name: f
; CHECK:       [[A:%[0-9]+]]:_(s64) = COPY $r32
; CHECK-NEXT:  [[S:%[0-9]+]]:_(s64) = G_BSWAP [[A]]
; CHECK-NEXT:  [[C40:%[0-9]+]]:_(s64) = G_CONSTANT i64 40
; CHECK-NEXT:  %{{[0-9]+}}:_(s64) = G_LSHR [[S]], [[C40]](s64)
; CHECK:       [[C7:%[0-9]+]]:_(s64) = G_CONSTANT i64 7
; CHECK:       [[R:%[0-9]+]]:_(s64) = G_LSHR %{{[0-9]+}}, [[C7]](s64)
; CHECK-NEXT:  $r8 = COPY [[R]](s64)
define i17 @f(i17 %a) {
  %r = call i17 @llvm.bitreverse.i17(i17 %a)
  ret i17 %r
}
