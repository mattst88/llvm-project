; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The library rounds a value to an integer, and returns a long of 64 bits.
; A result of fewer is the low bits of that.

define i32 @lround_float(float %a) {
; CHECK-LABEL: lround_float:
; CHECK:       br.call.sptk.many b0 = lroundf ;;
  %r = call i32 @llvm.lround.i32.f32(float %a)
  ret i32 %r
}

define i32 @lround_double(double %a) {
; CHECK-LABEL: lround_double:
; CHECK:       br.call.sptk.many b0 = lround ;;
  %r = call i32 @llvm.lround.i32.f64(double %a)
  ret i32 %r
}

define i32 @lround_extended(x86_fp80 %a) {
; CHECK-LABEL: lround_extended:
; CHECK:       br.call.sptk.many b0 = lroundl ;;
  %r = call i32 @llvm.lround.i32.f80(x86_fp80 %a)
  ret i32 %r
}

define i32 @lround_quad(fp128 %a) {
; CHECK-LABEL: lround_quad:
; CHECK:       br.call.sptk.many b0 = lroundf128 ;;
  %r = call i32 @llvm.lround.i32.f128(fp128 %a)
  ret i32 %r
}

define i32 @llround_double(double %a) {
; CHECK-LABEL: llround_double:
; CHECK:       br.call.sptk.many b0 = llround ;;
  %r = call i32 @llvm.llround.i32.f64(double %a)
  ret i32 %r
}

define i32 @lrint_double(double %a) {
; CHECK-LABEL: lrint_double:
; CHECK:       br.call.sptk.many b0 = lrint ;;
  %r = call i32 @llvm.lrint.i32.f64(double %a)
  ret i32 %r
}

define i64 @lround_long(double %a) {
; CHECK-LABEL: lround_long:
; CHECK:       br.call.sptk.many b0 = lround ;;
  %r = call i64 @llvm.lround.i64.f64(double %a)
  ret i64 %r
}
