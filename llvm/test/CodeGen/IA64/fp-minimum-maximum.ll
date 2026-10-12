; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; fmin and fmax choose their second source when the two have no order, and
; either of two zeroes. The minimum and the maximum that IEEE 754-2019
; defines are a NaN if either operand is one, or with "num" the operand that
; is not one, and take a negative zero to be the lesser. What fmin and fmax
; leave undone is a compare for the NaN and an fclass for the zero.

define float @minimum_float(float %a, float %b) {
; CHECK-LABEL: minimum_float:
; CHECK-DAG:   fclass.m p6, p7 = f8, 6
; CHECK-DAG:   fmin f6 = f8, f9
; CHECK-DAG:   fcmp.unord p6, p7 = f8, f9
; CHECK-DAG:   fcmp.eq p6, p7 = f8, f9
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call float @llvm.minimum.f32(float %a, float %b)
  ret float %r
}

define double @maximum_double(double %a, double %b) {
; CHECK-LABEL: maximum_double:
; CHECK-DAG:   fclass.m p6, p7 = f8, 5
; CHECK-DAG:   fmax f6 = f8, f9
; CHECK-DAG:   fcmp.unord p6, p7 = f8, f9
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call double @llvm.maximum.f64(double %a, double %b)
  ret double %r
}

define x86_fp80 @maximum_extended(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: maximum_extended:
; CHECK-DAG:   fclass.m p6, p7 = f8, 5
; CHECK-DAG:   fmax f6 = f8, f9
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call x86_fp80 @llvm.maximum.f80(x86_fp80 %a, x86_fp80 %b)
  ret x86_fp80 %r
}

define float @minimumnum_float(float %a, float %b) {
; CHECK-LABEL: minimumnum_float:
; CHECK:       fcmp.unord p6, p7 = f8, f8
; CHECK:       (p6) mov f8 = f9
; CHECK:       fcmp.unord p6, p7 = f9, f9
; CHECK:       (p6) mov f9 = f8
; CHECK-DAG:   fclass.m p6, p7 = f8, 6
; CHECK-DAG:   fmin f6 = f8, f9
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call float @llvm.minimumnum.f32(float %a, float %b)
  ret float %r
}

define x86_fp80 @maximumnum_extended(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: maximumnum_extended:
; CHECK-DAG:   fclass.m p6, p7 = f8, 5
; CHECK-DAG:   fmax f6 = f8, f9
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call x86_fp80 @llvm.maximumnum.f80(x86_fp80 %a, x86_fp80 %b)
  ret x86_fp80 %r
}

; Nothing has to be found out about operands that are neither NaNs nor
; zeroes of either sign.
define double @minimum_fast(double %a, double %b) {
; CHECK-LABEL: minimum_fast:
; CHECK:       fmin f8 = f8, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call nnan nsz double @llvm.minimum.f64(double %a, double %b)
  ret double %r
}

; A quadruple-precision value is the library's.
define fp128 @minimum_quad(fp128 %a, fp128 %b) {
; CHECK-LABEL: minimum_quad:
; CHECK:       br.call.sptk.many b0 = fminimumf128
  %r = call fp128 @llvm.minimum.f128(fp128 %a, fp128 %b)
  ret fp128 %r
}

define <2 x double> @maximum_vector(<2 x double> %a, <2 x double> %b) {
; CHECK-LABEL: maximum_vector:
; CHECK-DAG:   fmax f6 = f8, f10
; CHECK-DAG:   fmax f7 = f9, f11
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call <2 x double> @llvm.maximum.v2f64(<2 x double> %a, <2 x double> %b)
  ret <2 x double> %r
}
