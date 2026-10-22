; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A multiply and add that is given f0 to add is a multiply alone: a product
; of -0.0 stays negative, where the sum of it and 0.0 is positive. So a 0.0
; that is to be added is first copied out of f0.

define double @add(double %a) {
; CHECK-LABEL: add:
; CHECK:       mov f6 = f0 ;;
; CHECK-NEXT:  fma.d f8 = f8, f1, f6
  %r = fadd double %a, 0.0
  ret double %r
}

define double @fma(double %a, double %b) {
; CHECK-LABEL: fma:
; CHECK:       mov f6 = f0 ;;
; CHECK-NEXT:  fma.d f8 = f8, f9, f6
  %r = call double @llvm.fma.f64(double %a, double %b, double 0.0)
  ret double %r
}

define double @fnma(double %a, double %b) {
; CHECK-LABEL: fnma:
; CHECK:       mov f6 = f0 ;;
; CHECK-NEXT:  fnma.d f8 = f8, f9, f6
  %n = fneg double %a
  %r = call double @llvm.fma.f64(double %n, double %b, double 0.0)
  ret double %r
}

; As a factor f0 is 0.0 like any other.
define double @sub(double %a) {
; CHECK-LABEL: sub:
; CHECK:       fms.d f8 = f0, f1, f8
  %r = fsub double 0.0, %a
  ret double %r
}

define double @mul(double %a) {
; CHECK-LABEL: mul:
; CHECK:       fma.d f8 = f8, f0, f0
  %r = fmul double %a, 0.0
  ret double %r
}

; The last step of a division adds the dividend.
define float @div(float %b) {
; CHECK-LABEL: div:
; CHECK:       frcpa f7, p8 = f0, f8
; CHECK-NEXT:  mov f6 = f0 ;;
; CHECK:       (p8) fnma.s1 f6 = f8, f9, f6
  %r = fdiv float 0.0, %b
  ret float %r
}

declare double @llvm.fma.f64(double, double, double)
