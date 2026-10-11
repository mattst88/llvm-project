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
; CHECK:         frcpa f6, p6 = f8, f9 ;;
; CHECK-NEXT:    (p6) fma.s1 f7 = f8, f6, f0
; CHECK-NEXT:    (p6) fnma.s1 f10 = f9, f6, f1 ;;
; CHECK-NEXT:    (p6) fma.s1 f7 = f10, f7, f7
; CHECK-NEXT:    (p6) fma.s1 f11 = f10, f10, f0
; CHECK-NEXT:    (p6) fma.s1 f6 = f10, f6, f6 ;;
; CHECK-NEXT:    (p6) fma.s.s1 f7 = f11, f7, f7
; CHECK-NEXT:    (p6) fma.s1 f6 = f11, f6, f6 ;;
; CHECK-NEXT:    (p6) fnma.s1 f10 = f9, f7, f8 ;;
; CHECK-NEXT:    (p6) fma.s f6 = f10, f6, f7 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv float %a, %b
  ret float %r
}

; A double has more than twice the bits, and takes one more step.

define double @div_double(double %a, double %b) {
; CHECK-LABEL: div_double:
; CHECK:         frcpa f6, p6 = f8, f9 ;;
; CHECK-NEXT:    (p6) fma.s1 f7 = f8, f6, f0
; CHECK-NEXT:    (p6) fnma.s1 f10 = f9, f6, f1 ;;
; CHECK-NEXT:    (p6) fma.s1 f7 = f10, f7, f7
; CHECK-NEXT:    (p6) fma.s1 f11 = f10, f10, f0
; CHECK-NEXT:    (p6) fma.s1 f6 = f10, f6, f6 ;;
; CHECK-NEXT:    (p6) fma.s1 f7 = f11, f7, f7
; CHECK-NEXT:    (p6) fma.s1 f10 = f11, f11, f0
; CHECK-NEXT:    (p6) fma.s1 f6 = f11, f6, f6 ;;
; CHECK-NEXT:    (p6) fma.d.s1 f7 = f10, f7, f7
; CHECK-NEXT:    (p6) fma.s1 f6 = f10, f6, f6 ;;
; CHECK-NEXT:    (p6) fnma.s1 f11 = f9, f7, f8 ;;
; CHECK-NEXT:    (p6) fma.d f6 = f11, f6, f7 ;;
; CHECK-NEXT:    mov f8 = f6
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, %b
  ret double %r
}

; The predicate is the one a comparison writes, and the comparison comes after
; the quotient that the choice it makes is of.

define double @div_select(double %a, double %b, double %x) {
; CHECK-LABEL: div_select:
; CHECK:         frcpa f6, p6 = f8, f9 ;;
; CHECK:         (p6) fma.d f6 = f12, f6, f7
; CHECK-NEXT:    fcmp.lt p6, p7 = f8, f10 ;;
; CHECK-NEXT:    (p6) mov f10 = f6 ;;
; CHECK-NEXT:    mov f8 = f10
  %c = fcmp olt double %a, %x
  %d = fdiv double %a, %b
  %r = select i1 %c, double %d, double %x
  ret double %r
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
