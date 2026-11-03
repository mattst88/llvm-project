; RUN: llc -mtriple=ia64-unknown-linux-gnu -mcpu=madison -verify-machineinstrs < %s | FileCheck %s

; A constant added to a sum moves onto the addend that is ready first, so
; that the one loaded last is added last: a single add between the load and
; the address it feeds, not a shladd and then an adds.

; CHECK-LABEL: late_base:
; CHECK: shladd [[S:r[0-9]+]] = r33, 4, r0
; CHECK: ld8 [[B:r[0-9]+]] = [{{r[0-9]+}}]
; CHECK: adds [[O:r[0-9]+]] = 8, [[S]] ;;
; CHECK-NEXT: add [[A:r[0-9]+]] = [[B]], [[O]] ;;
; CHECK-NEXT: ld8 r8 = [[[A]]]
define i64 @late_base(ptr %pp, i64 %i) {
  %a = load ptr, ptr %pp
  %b = load ptr, ptr %a
  %e = getelementptr [16 x i8], ptr %b, i64 %i, i64 8
  %v = load i64, ptr %e
  ret i64 %v
}

; CHECK-LABEL: late_index:
; CHECK: ld8 [[I:r[0-9]+]] = [r33]
; CHECK: adds [[O:r[0-9]+]] = 1096, r32 ;;
; CHECK-NEXT: shladd [[A:r[0-9]+]] = [[I]], 3, [[O]] ;;
; CHECK-NEXT: ld8 r8 = [[[A]]]
define i64 @late_index(ptr %b, ptr %ip) {
  %i = load i64, ptr %ip
  %e = getelementptr i64, ptr %b, i64 %i
  %f = getelementptr i8, ptr %e, i64 1096
  %v = load i64, ptr %f
  ret i64 %v
}

; CHECK-LABEL: late_add:
; CHECK: ld8 [[L:r[0-9]+]] = [r32]
; CHECK: adds [[O:r[0-9]+]] = 16, r33 ;;
; CHECK-NEXT: add r8 = [[L]], [[O]]
define i64 @late_add(ptr %pp, i64 %x) {
  %a = load i64, ptr %pp
  %s = add i64 %a, %x
  %t = add i64 %s, 16
  ret i64 %t
}

; With both addends ready together, nothing changes.
; CHECK-LABEL: both_ready:
; CHECK: add [[S:r[0-9]+]] = r32, r33 ;;
; CHECK-NEXT: adds r8 = 16, [[S]]
define i64 @both_ready(i64 %a, i64 %x) {
  %s = add i64 %a, %x
  %t = add i64 %s, 16
  ret i64 %t
}
