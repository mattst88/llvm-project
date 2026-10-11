; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; fmin chooses its first source when that is less than its second and its
; second otherwise, and fmax chooses its first source when its second is less
; than it. Either therefore chooses its second source when the two are
; unordered or are zeroes of either sign, and a select does the same only when
; its operands are in the matching order.

define double @min_olt(double %a, double %b) {
; CHECK-LABEL: min_olt:
; CHECK:         fmin f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp olt double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define double @max_olt(double %a, double %b) {
; CHECK-LABEL: max_olt:
; CHECK:         fmax f8 = f9, f8
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp olt double %a, %b
  %r = select i1 %c, double %b, double %a
  ret double %r
}

define double @max_ogt(double %a, double %b) {
; CHECK-LABEL: max_ogt:
; CHECK:         fmax f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define double @min_ogt(double %a, double %b) {
; CHECK-LABEL: min_ogt:
; CHECK:         fmin f8 = f9, f8
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %b, double %a
  ret double %r
}

; A comparison that is true of unordered operands is the opposite of one that
; is false of them.

define double @max_ule(double %a, double %b) {
; CHECK-LABEL: max_ule:
; CHECK:         fmax f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp ule double %a, %b
  %r = select i1 %c, double %b, double %a
  ret double %r
}

define double @min_ule(double %a, double %b) {
; CHECK-LABEL: min_ule:
; CHECK:         fmin f8 = f9, f8
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp ule double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define double @min_uge(double %a, double %b) {
; CHECK-LABEL: min_uge:
; CHECK:         fmin f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp uge double %a, %b
  %r = select i1 %c, double %b, double %a
  ret double %r
}

define double @max_uge(double %a, double %b) {
; CHECK-LABEL: max_uge:
; CHECK:         fmax f8 = f9, f8
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp uge double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define float @min_float(float %a, float %b) {
; CHECK-LABEL: min_float:
; CHECK:         fmin f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp olt float %a, %b
  %r = select i1 %c, float %a, float %b
  ret float %r
}

define x86_fp80 @max_extended(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: max_extended:
; CHECK:         fmax f8 = f8, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp ogt x86_fp80 %a, %b
  %r = select i1 %c, x86_fp80 %a, x86_fp80 %b
  ret x86_fp80 %r
}

; A comparison that is true of equal operands chooses the other zero of two,
; and one that chooses among other values is not a minimum at all.

define double @not_min_ole(double %a, double %b) {
; CHECK-LABEL: not_min_ole:
; CHECK-NOT:     fmin
; CHECK:         fcmp.le p6, p7 = f8, f9
; CHECK:         br.ret.sptk.many b0 ;;
  %c = fcmp ole double %a, %b
  %r = select i1 %c, double %a, double %b
  ret double %r
}

define double @not_min_other(double %a, double %b, double %x, double %y) {
; CHECK-LABEL: not_min_other:
; CHECK-NOT:     fmin
; CHECK:         fcmp.lt p6, p7 = f8, f9
; CHECK:         br.ret.sptk.many b0 ;;
  %c = fcmp olt double %a, %b
  %r = select i1 %c, double %x, double %y
  ret double %r
}
