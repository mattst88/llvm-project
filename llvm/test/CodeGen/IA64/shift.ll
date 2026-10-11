; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

define i64 @shl(i64 %a, i64 %b) {
; CHECK-LABEL: shl:
; CHECK:       shl r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = shl i64 %a, %b
  ret i64 %r
}

define i64 @lshr(i64 %a, i64 %b) {
; CHECK-LABEL: lshr:
; CHECK:       shr.u r8 = r32, r33
  %r = lshr i64 %a, %b
  ret i64 %r
}

define i64 @ashr(i64 %a, i64 %b) {
; CHECK-LABEL: ashr:
; CHECK:       shr r8 = r32, r33
  %r = ashr i64 %a, %b
  ret i64 %r
}

; By a constant these are a deposit and an extract, written as shifts.

define i64 @shl_constant(i64 %a) {
; CHECK-LABEL: shl_constant:
; CHECK:       shl r8 = r32, 5
  %r = shl i64 %a, 5
  ret i64 %r
}

define i64 @shl_most(i64 %a) {
; CHECK-LABEL: shl_most:
; CHECK:       shl r8 = r32, 63
  %r = shl i64 %a, 63
  ret i64 %r
}

define i64 @lshr_constant(i64 %a) {
; CHECK-LABEL: lshr_constant:
; CHECK:       shr.u r8 = r32, 17
  %r = lshr i64 %a, 17
  ret i64 %r
}

define i64 @lshr_most(i64 %a) {
; CHECK-LABEL: lshr_most:
; CHECK:       shr.u r8 = r32, 63
  %r = lshr i64 %a, 63
  ret i64 %r
}

define i64 @ashr_constant(i64 %a) {
; CHECK-LABEL: ashr_constant:
; CHECK:       shr r8 = r32, 1
  %r = ashr i64 %a, 1
  ret i64 %r
}

; A shift of up to four bits goes with an addition.

define i64 @shladd1(i64 %a, i64 %b) {
; CHECK-LABEL: shladd1:
; CHECK:       shladd r8 = r32, 1, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %s = shl i64 %a, 1
  %r = add i64 %s, %b
  ret i64 %r
}

define i64 @shladd4(i64 %a, i64 %b) {
; CHECK-LABEL: shladd4:
; CHECK:       shladd r8 = r32, 4, r33
  %s = shl i64 %a, 4
  %r = add i64 %b, %s
  ret i64 %r
}

define i64 @shladd5(i64 %a, i64 %b) {
; CHECK-LABEL: shladd5:
; CHECK:       shl [[S:r[0-9]+]] = r32, 5 ;;
; CHECK-NEXT:  add r8 = r33, [[S]]
  %s = shl i64 %a, 5
  %r = add i64 %b, %s
  ret i64 %r
}

define i64 @index(ptr %p, i64 %i) {
; CHECK-LABEL: index:
; CHECK:       shladd [[A:r[0-9]+]] = r33, 3, r32 ;;
; CHECK-NEXT:  ld8 r8 = [[[A]]]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %q
  ret i64 %v
}
