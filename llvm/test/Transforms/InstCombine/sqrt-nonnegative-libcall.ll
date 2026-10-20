; RUN: opt < %s -passes=instcombine -S | FileCheck %s

; sqrt sets errno only for an argument less than zero.  When the argument
; cannot be one, the libcall is the intrinsic, and needs no check for the
; domain error.

; CHECK-LABEL: @sum_of_squares(
; CHECK:         call double @llvm.sqrt.f64(
; CHECK-NOT:     @sqrt(
define double @sum_of_squares(double %x, double %y) {
  %xx = fmul double %x, %x
  %yy = fmul double %y, %y
  %s = fadd double %xx, %yy
  %e = fadd double %s, 1.0e-4
  %r = call double @sqrt(double %e)
  ret double %r
}

; CHECK-LABEL: @fabs_float(
; CHECK:         call float @llvm.sqrt.f32(
define float @fabs_float(float %x) {
  %a = call float @llvm.fabs.f32(float %x)
  %r = call float @sqrtf(float %a)
  ret float %r
}

; The flags and metadata of the call carry over.
; CHECK-LABEL: @flags(
; CHECK:         call nsz double @llvm.sqrt.f64({{.*}}), !fpmath
define double @flags(double %x) {
  %xx = fmul double %x, %x
  %r = call nsz double @sqrt(double %xx), !fpmath !0
  ret double %r
}

; A product of two different values can be negative.
; CHECK-LABEL: @maybe_negative(
; CHECK:         call double @sqrt(
define double @maybe_negative(double %x, double %y) {
  %a = fmul double %x, %y
  %r = call double @sqrt(double %a)
  ret double %r
}

declare double @sqrt(double)
declare float @sqrtf(float)
declare float @llvm.fabs.f32(float)

!0 = !{float 2.5}
