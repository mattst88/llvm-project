; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A byte compared unsigned after an add is promoted to a compare of the
; masked sum. With the byte zero-extended, it is a test that the byte is in
; a range, without the zxt1.

; c - '0' <= 9, as InstCombine leaves it: (c - 58) <u 246 in i8, true
; outside '0'-'9'.
; CHECK-LABEL: not_digit:
; CHECK:         ld1 [[C:r[0-9]+]] = [r32]
; CHECK:         adds [[D:r[0-9]+]] = -48, [[C]]
; CHECK-NOT:     zxt1
; CHECK:         cmp.ltu p6, p7 = 9, [[D]]
define i1 @not_digit(ptr %p) {
  %c = load i8, ptr %p
  %a = add i8 %c, -58
  %d = icmp ult i8 %a, -10
  ret i1 %d
}

; (c - 'a') <u 26: the range does not wrap.
; CHECK-LABEL: lower:
; CHECK:         ld1 [[C:r[0-9]+]] = [r32]
; CHECK:         adds [[D:r[0-9]+]] = -97, [[C]]
; CHECK-NOT:     zxt1
; CHECK:         cmp.ltu p7, p6 = 25, [[D]]
define i1 @lower(ptr %p) {
  %c = load i8, ptr %p
  %a = add i8 %c, -97
  %d = icmp ult i8 %a, 26
  ret i1 %d
}

; A byte not known to be zero-extended keeps the mask.
; CHECK-LABEL: unknown:
; CHECK:         zxt1
define i1 @unknown(i8 %c) {
  %a = add i8 %c, -97
  %d = icmp ult i8 %a, 26
  ret i1 %d
}
