; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; There is no instruction that divides. frcpa gives the reciprocal of the
; divisor to under nine bits, and a predicate that is clear when an operand is
; a zero, an infinity or a NaN and what it gave is the quotient. Each step from
; there doubles the bits that are right, in status field 1 so that no flag of
; a step is one the program sees, and the last adds what the remainder says is
; missing and rounds once, as the program asked.

define float @div_float(float %a, float %b) {
; CHECK-LABEL: div_float:
; CHECK:         frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f10 = f9, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f10, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f11 = f10, f10, f0
; CHECK-NEXT:    (p8) fma.s1 f10 = f10, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s.s1 f7 = f11, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f10 = f11, f10, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1 f8 = f9, f7, f8 ;;
; CHECK-NEXT:    (p8) fma.s f6 = f8, f10, f7 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv float %a, %b
  ret float %r
}

; A double has more than twice the bits, and takes one more step.

define double @div_double(double %a, double %b) {
; CHECK-LABEL: div_double:
; CHECK:         frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f10 = f9, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f10, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f11 = f10, f10, f0
; CHECK-NEXT:    (p8) fma.s1 f10 = f10, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f11, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f12 = f11, f11, f0
; CHECK-NEXT:    (p8) fma.s1 f10 = f11, f10, f10 ;;
; CHECK-NEXT:    (p8) fma.d.s1 f7 = f12, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f10 = f12, f10, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1 f8 = f9, f7, f8 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f8, f10, f7 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, %b
  ret double %r
}

; When the dividend is 1, q = a * y is y, and the steps that would make it
; again from y's are left out until the one that rounds it.

define double @recip_double(double %b) {
; CHECK-LABEL: recip_double:
; CHECK:         frcpa f6, p8 = f1, f8 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f8, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f7, f7, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f9, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f9, f7, f7 ;;
; CHECK-NEXT:    (p8) fma.d.s1 f9 = f10, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f7 = f10, f7, f7 ;;
; CHECK-NEXT:    (p8) fnma.s1 f8 = f8, f9, f1 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f8, f7, f9 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double 1.0, %b
  ret double %r
}

define float @recip_float(float %b) {
; CHECK-LABEL: recip_float:
; CHECK:         frcpa f6, p8 = f1, f8 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f8, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f7, f7, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s.s1 f10 = f9, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f7 = f9, f7, f7 ;;
; CHECK-NEXT:    (p8) fnma.s1 f8 = f8, f10, f1 ;;
; CHECK-NEXT:    (p8) fma.s f6 = f8, f7, f10 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv float 1.0, %b
  ret float %r
}

; The predicate frcpa writes is not the one a comparison writes, so the
; comparison need not wait for the quotient that the choice it makes is of.

define double @div_select(double %a, double %b, double %x) {
; CHECK-LABEL: div_select:
; CHECK:         frcpa f6, p8 = f8, f9
; CHECK-NEXT:    fcmp.lt p6, p7 = f8, f10 ;;
; CHECK:         (p8) fma.d f6 = f9, f11, f7 ;;
; CHECK-NEXT:    (p6) mov f10 = f6 ;;
; CHECK-NEXT:    mov f8 = f10
  %c = fcmp olt double %a, %x
  %d = fdiv double %a, %b
  %r = select i1 %c, double %d, double %x
  ret double %r
}

; Each division has a predicate of its own, so the steps of two divisions that
; do not depend on each other are in the same groups.

define double @two_div(double %a, double %b, double %c, double %d) {
; CHECK-LABEL: two_div:
; CHECK:         frcpa f6, p8 = f8, f9
; CHECK-NEXT:    frcpa f7, p9 = f10, f11 ;;
; CHECK-NEXT:    (p8) fma.s1 f12 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f13 = f9, f6, f1
; CHECK-NEXT:    (p9) fma.s1 f14 = f10, f7, f0
; CHECK-NEXT:    (p9) fnma.s1 f15 = f11, f7, f1 ;;
; CHECK:         (p8) fma.d f6 = f8, f13, f12
; CHECK-NEXT:    (p9) fma.d f7 = f10, f9, f14 ;;
; CHECK-NEXT:    fma.d f8 = f6, f1, f7
  %x = fdiv double %a, %b
  %y = fdiv double %c, %d
  %s = fadd double %x, %y
  ret double %s
}

; A quotient that is a product is one: the reciprocal of a power of two is
; exact.

define double @div_power_of_two(double %a) {
; CHECK-LABEL: div_power_of_two:
; CHECK-NOT:     frcpa
; CHECK:         fma.d f8 = f8, f6, f0
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 4.0
  ret double %r
}

; With a constant divisor b, y = 1 / b rounded to double gives q = a * y, and
; q + (a - b * q) * y rounds to a / b: a chain of three steps, which need not
; wait for frcpa. frcpa still divides zeros, infinities and NaNs.

define double @div_const(double %a) {
; CHECK-LABEL: div_const:
; CHECK:         movl r14 = 4592670820000712476
; CHECK-NEXT:    movl r15 = 4621256167635550208 ;;
; CHECK-NEXT:    setf.d f7 = r14
; CHECK-NEXT:    setf.d f9 = r15 ;;
; CHECK-NEXT:    fma.s1 f10 = f8, f7, f0
; CHECK-NEXT:    frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    fnma.s1 f8 = f9, f10, f8 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f8, f7, f10 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 9.0
  ret double %r
}

define float @div_const_float(float %a) {
; CHECK-LABEL: div_const_float:
; CHECK:         movl r14 = 4599676419421066581
; CHECK-NEXT:    movl r15 = 1077936128 ;;
; CHECK-NEXT:    setf.d f7 = r14
; CHECK-NEXT:    setf.s f9 = r15 ;;
; CHECK-NEXT:    fma.s1 f10 = f8, f7, f0
; CHECK-NEXT:    frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    fnma.s1 f8 = f9, f10, f8 ;;
; CHECK-NEXT:    (p8) fma.s f6 = f8, f7, f10 ;;
  %r = fdiv float %a, 3.0
  ret float %r
}

; -1.5 is -3 * 2^-1.

define double @div_const_fraction(double %a) {
; CHECK-LABEL: div_const_fraction:
; CHECK:         movl r14 = -4619192017806338731
; CHECK-NEXT:    movl r15 = -4613937818241073152 ;;
; CHECK:         fnma.s1 f8 = f9, f10, f8 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f8, f7, f10 ;;
  %r = fdiv double %a, -1.5
  ret double %r
}

; A subnormal quotient by a multiple of 2 can be a midpoint, as 5 * 2^-1074
; divided by 10 is, and only the full sequence rounds that to even.

define double @div_const_even(double %a) {
; CHECK-LABEL: div_const_even:
; CHECK:         frcpa f6, p8 = f8, f7 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f10 = f7, f6, f1 ;;
  %r = fdiv double %a, 10.0
  ret double %r
}

; Nor is the sum close enough to a / b to be sure of rounding right when b's
; significand has more than 32 bits, as that of 1/3 does.

define double @div_const_long(double %a) {
; CHECK-LABEL: div_const_long:
; CHECK:         frcpa f6, p8 = f8, f7 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f10 = f7, f6, f1 ;;
  %r = fdiv double %a, 0x3FD5555555555555
  ret double %r
}

; Strict division keeps the full sequence, which rounds in any mode.

define double @div_const_strict(double %a) strictfp {
; CHECK-LABEL: div_const_strict:
; CHECK:         frcpa f6, p8 = f8, f7 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f8, f6, f0
; CHECK-NEXT:    (p8) fnma.s1 f10 = f7, f6, f1 ;;
  %r = call double @llvm.experimental.constrained.fdiv.f64(double %a, double 9.0, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ret double %r
}
