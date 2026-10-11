; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

define i64 @sext8(i8 %a) {
; CHECK-LABEL: sext8:
; CHECK:       sxt1 r8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sext i8 %a to i64
  ret i64 %r
}

define i64 @sext16(i16 %a) {
; CHECK-LABEL: sext16:
; CHECK:       sxt2 r8 = r32
  %r = sext i16 %a to i64
  ret i64 %r
}

define i64 @sext32(i32 %a) {
; CHECK-LABEL: sext32:
; CHECK:       sxt4 r8 = r32
  %r = sext i32 %a to i64
  ret i64 %r
}

define i64 @zext8(i8 %a) {
; CHECK-LABEL: zext8:
; CHECK:       zxt1 r8 = r32
  %r = zext i8 %a to i64
  ret i64 %r
}

define i64 @zext16(i16 %a) {
; CHECK-LABEL: zext16:
; CHECK:       zxt2 r8 = r32
  %r = zext i16 %a to i64
  ret i64 %r
}

define i64 @zext32(i32 %a) {
; CHECK-LABEL: zext32:
; CHECK:       zxt4 r8 = r32
  %r = zext i32 %a to i64
  ret i64 %r
}

define i64 @sext1(i1 %a) {
; CHECK-LABEL: sext1:
; CHECK:       and [[T:r[0-9]+]] = 1, r32 ;;
; CHECK-NEXT:  sub r8 = 0, [[T]]
  %r = sext i1 %a to i64
  ret i64 %r
}

define i64 @zext1(i1 %a) {
; CHECK-LABEL: zext1:
; CHECK:       and r8 = 1, r32
  %r = zext i1 %a to i64
  ret i64 %r
}

; The bits above an argument are nobody's, so what the caller asked of a
; result has to be made.

define signext i32 @sext_result(i32 %a, i32 %b) {
; CHECK-LABEL: sext_result:
; CHECK:       add [[T:r[0-9]+]] = r32, r33 ;;
; CHECK-NEXT:  sxt4 r8 = [[T]]
  %r = add i32 %a, %b
  ret i32 %r
}

define zeroext i16 @zext_result(i16 %a, i16 %b) {
; CHECK-LABEL: zext_result:
; CHECK:       add [[T:r[0-9]+]] = r32, r33 ;;
; CHECK-NEXT:  zxt2 r8 = [[T]]
  %r = add i16 %a, %b
  ret i16 %r
}

; A load extends with zeros and no other way.

define i64 @sext_load(ptr %p) {
; CHECK-LABEL: sext_load:
; CHECK:       ld4 [[T:r[0-9]+]] = [r32] ;;
; CHECK-NEXT:  sxt4 r8 = [[T]]
  %v = load i32, ptr %p
  %r = sext i32 %v to i64
  ret i64 %r
}

; A narrow shift right needs the bits above the value to be its own, and
; the count of any narrow shift is all of the register.

define i32 @lshr32(i32 %a, i32 %b) {
; CHECK-LABEL: lshr32:
; CHECK-DAG:   zxt4 [[A:r[0-9]+]] = r32
; CHECK-DAG:   zxt4 [[B:r[0-9]+]] = r33
; CHECK:       shr.u r8 = [[A]], [[B]]
  %r = lshr i32 %a, %b
  ret i32 %r
}

define i32 @ashr32(i32 %a, i32 %b) {
; CHECK-LABEL: ashr32:
; CHECK-DAG:   sxt4 [[A:r[0-9]+]] = r32
; CHECK-DAG:   zxt4 [[B:r[0-9]+]] = r33
; CHECK:       shr r8 = [[A]], [[B]]
  %r = ashr i32 %a, %b
  ret i32 %r
}
