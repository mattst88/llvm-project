; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; No instruction divides integers. The FP unit divides them as significands:
; converted to FP values, frcpa's approximation of 1 / b refined twice, the
; quotient corrected by its remainder, and truncated back to an integer. Of
; integers, frcpa gives the quotient itself only when the dividend is 0, and
; every step then gives 0 too, so no step waits for frcpa's predicate.
define i64 @udiv(i64 %a, i64 %b) {
; CHECK-LABEL: udiv:
; CHECK:       setf.sig f6 = r33
; CHECK-NEXT:  setf.sig f7 = r32 ;;
; CHECK-NEXT:  fma.s1 f7 = f7, f1, f0
; CHECK-NEXT:  fma.s1 f6 = f6, f1, f0 ;;
; CHECK-NEXT:  frcpa.s1 f8, p8 = f7, f6 ;;
; CHECK-NEXT:  fnma.s1 f9 = f6, f8, f1 ;;
; CHECK-NEXT:  fma.s1 f10 = f9, f9, f0
; CHECK-NEXT:  fma.s1 f8 = f8, f9, f8 ;;
; CHECK-NEXT:  fma.s1 f8 = f8, f10, f8 ;;
; CHECK-NEXT:  fma.s1 f9 = f8, f7, f0 ;;
; CHECK-NEXT:  fnma.s1 f6 = f6, f9, f7 ;;
; CHECK-NEXT:  fma.s1 f6 = f6, f8, f9 ;;
; CHECK-NEXT:  fcvt.fxu.trunc.s1 f6 = f6 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = udiv i64 %a, %b
  ret i64 %r
}

; Signed integers are converted with fcvt.xf and back with fcvt.fx.
define i64 @sdiv(i64 %a, i64 %b) {
; CHECK-LABEL: sdiv:
; CHECK:       fcvt.xf f7 = f7
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK:       fcvt.fx.trunc.s1 f6 = f6 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %r = sdiv i64 %a, %b
  ret i64 %r
}

; The remainder is a + q * -b, without leaving the FP unit.
define i64 @srem(i64 %a, i64 %b) {
; CHECK-LABEL: srem:
; CHECK:       sub [[NEG:r[0-9]+]] = 0, r33
; CHECK:       setf.sig [[FNEG:f[0-9]+]] = [[NEG]]
; CHECK:       fcvt.fx.trunc.s1 [[Q:f[0-9]+]] = {{f[0-9]+}} ;;
; CHECK-NEXT:  xma.l [[R:f[0-9]+]] = [[Q]], [[FNEG]], f7 ;;
; CHECK-NEXT:  getf.sig r8 = [[R]]
  %r = srem i64 %a, %b
  ret i64 %r
}

; The quotient and remainder of the same operands share the division.
define i64 @both(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: both:
; CHECK:       frcpa
; CHECK-NOT:   frcpa
; CHECK:       fcvt.fxu.trunc.s1 f6 = f6 ;;
; CHECK-NEXT:  getf.sig r15 = f6
; CHECK-NEXT:  xma.l f7 = f6, f8, f7 ;;
; CHECK-NEXT:  getf.sig r8 = f7
; CHECK-NEXT:  st8 [r34] = r15
  %q = udiv i64 %a, %b
  %r = urem i64 %a, %b
  store i64 %q, ptr %p
  ret i64 %r
}

; Integers that fit in 32 bits need one refinement, plus 2^-34 so that the
; quotient does not fall short of an exact integer.
define i32 @urem32(i32 %a, i32 %b) {
; CHECK-LABEL: urem32:
; CHECK:       movl r16 = 4454060031469420544
; CHECK:       setf.d f10 = r16
; CHECK:       frcpa.s1 f9, p8 = f8, f6 ;;
; CHECK-NEXT:  fnma.s1 f6 = f6, f9, f1
; CHECK-NEXT:  fma.s1 f8 = f8, f9, f0 ;;
; CHECK-NEXT:  fma.s1 f8 = f8, f6, f8
; CHECK-NEXT:  fma.s1 f6 = f6, f6, f10 ;;
; CHECK-NEXT:  fma.s1 f6 = f6, f8, f8
; CHECK-NEXT:  setf.sig f8 = r17 ;;
; CHECK-NEXT:  fcvt.fxu.trunc.s1 f6 = f6 ;;
; CHECK-NEXT:  xma.l f6 = f6, f8, f7 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %r = urem i32 %a, %b
  ret i32 %r
}

define i32 @sdiv32(i32 %a, i32 %b) {
; CHECK-LABEL: sdiv32:
; CHECK:       fcvt.xf
; CHECK:       fma.s1 {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, f9 ;;
; CHECK-NEXT:  fma.s1
; CHECK-NEXT:  fcvt.fx.trunc.s1
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; The steps raise no FP exception, so a division used on one path sinks into it.
define i32 @sink_udiv(i32 %a, i32 %b, i1 %c) {
; CHECK-LABEL: sink_udiv:
; CHECK:       (p6) br.ret.dptk.many b0 ;;
; CHECK-NEXT:  .LBB{{[0-9_]+}}: // %use
; CHECK:       frcpa.s1
entry:
  %q = udiv i32 %a, %b
  br i1 %c, label %use, label %done

use:
  ret i32 %q

done:
  ret i32 0
}

; A call is smaller.
define i64 @sdiv_minsize(i64 %a, i64 %b) minsize {
; CHECK-LABEL: sdiv_minsize:
; CHECK:       br.call.sptk.many b0 = __divdi3 ;;
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @urem_minsize(i64 %a, i64 %b) minsize {
; CHECK-LABEL: urem_minsize:
; CHECK:       br.call.sptk.many b0 = __umoddi3 ;;
  %r = urem i64 %a, %b
  ret i64 %r
}
