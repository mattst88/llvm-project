; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; fclass says whether a value is of one of the classes its nine bits name. A
; NaN is tested for whatever the sign, and a zero, an unnormalized number, a
; normalized one and an infinity each only with the signs that are named.

define i1 @nan(double %a) {
; CHECK-LABEL: nan:
; CHECK:       fclass.m p6, p7 = f8, 192
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 3)
  ret i1 %r
}

define i1 @snan(float %a) {
; CHECK-LABEL: snan:
; CHECK:       fclass.m p6, p7 = f8, 64
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 1)
  ret i1 %r
}

define i1 @inf(x86_fp80 %a) {
; CHECK-LABEL: inf:
; CHECK:       fclass.m p6, p7 = f8, 35
  %r = call i1 @llvm.is.fpclass.f80(x86_fp80 %a, i32 516)
  ret i1 %r
}

define i1 @negative_zero(float %a) {
; CHECK-LABEL: negative_zero:
; CHECK:       fclass.m p6, p7 = f8, 6
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 32)
  ret i1 %r
}

; A denormal of any precision is unnormalized in a register.
define i1 @subnormal(float %a) {
; CHECK-LABEL: subnormal:
; CHECK:       fclass.m p6, p7 = f8, 11
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 144)
  ret i1 %r
}

define i1 @finite(double %a) {
; CHECK-LABEL: finite:
; CHECK:       fclass.m p6, p7 = f8, 31
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 504)
  ret i1 %r
}

; The classes of the two signs differ, and each sign has a test of its own.
define i1 @positive_normal_or_negative_inf(double %a) {
; CHECK-LABEL: positive_normal_or_negative_inf:
; CHECK-DAG:   fclass.m p6, p7 = f8, 17
; CHECK-DAG:   fclass.m p6, p7 = f8, 34
; CHECK:       or r8 =
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 260)
  ret i1 %r
}

; All but one class is that class tested for, and the other predicate.
define i1 @not_negative_zero(double %a) {
; CHECK-LABEL: not_negative_zero:
; CHECK:       fclass.m p7, p6 = f8, 6
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 991)
  ret i1 %r
}

; A test does not raise an exception, so it is the same where one would be
; seen.
define i1 @strict(double %a) strictfp {
; CHECK-LABEL: strict:
; CHECK:       fclass.m p6, p7 = f8, 192
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 3) strictfp
  ret i1 %r
}
