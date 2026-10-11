; RUN: llc -mtriple=ia64-unknown-linux-gnu -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s

; An instruction that rounds, or that may raise an exception, reads the
; status register to learn how.

define double @add(double %a, double %b) {
; CHECK-LABEL: name: add
; CHECK: nofpexcept FMA_D {{.*}}, implicit $ar40
  %r = fadd double %a, %b
  ret double %r
}

define i64 @to_integer(double %a) {
; CHECK-LABEL: name: to_integer
; CHECK: FCVT_FX_TRUNC {{.*}}, implicit $ar40
  %r = fptosi double %a to i64
  ret i64 %r
}

define i64 @less(double %a, double %b) {
; CHECK-LABEL: name: less
; CHECK: nofpexcept SETCCf {{.*}}, implicit $ar40
  %c = fcmp olt double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define double @quotient(double %a, double %b) {
; CHECK-LABEL: name: quotient
; CHECK: nofpexcept FDIV_D {{.*}}, implicit $ar40
  %r = fdiv double %a, %b
  ret double %r
}

; One that only moves bits does not.

define double @negate(double %a) {
; CHECK-LABEL: name: negate
; CHECK: FMERGE_NS
; CHECK-NOT: $ar40
; CHECK: BR_RET
  %r = fneg double %a
  ret double %r
}
