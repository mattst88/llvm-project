; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -stop-after=legalizer < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -o /dev/null < %s

; A shift is narrowed by halves. One of three registers is made one of four
; first, whose halves come down to a register; its own halves are a register
; and a half, which nothing takes apart.

; CHECK-LABEL: name: shr
; CHECK-NOT: s96
; CHECK-NOT: s192
; CHECK: G_LSHR %{{[0-9]+}}, %{{[0-9]+}}(s64)
; CHECK-NOT: s96
; CHECK-NOT: s192
; CHECK: BR_RET
define void @shr(ptr %p, i64 %n) {
  %x = load i192, ptr %p
  %m = zext i64 %n to i192
  %r = lshr i192 %x, %m
  store i192 %r, ptr %p
  ret void
}

; The multiply that tells its overflow is made at twice the width, and the
; high half is shifted down: a shift of 130 bits, in three registers.

; CHECK-LABEL: name: mul
; CHECK-NOT: s96
; CHECK: BR_RET
define i1 @mul(i65 %a, i65 %b, ptr %p) {
  %r = call {i65, i1} @llvm.smul.with.overflow.i65(i65 %a, i65 %b)
  %o = extractvalue {i65, i1} %r, 1
  %v = extractvalue {i65, i1} %r, 0
  store i65 %v, ptr %p
  ret i1 %o
}
