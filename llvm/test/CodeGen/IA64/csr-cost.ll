; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -regalloc-csr-first-time-cost=0 < %s | FileCheck %s --check-prefix=CSR
; a, b and c live across the call that sets errno after the inline sqrt,
; which rarely runs. They stay in scratch registers, saved around that call,
; rather than taking preserved ones that cost a spill and fill on every call.
; CHECK-LABEL: f:
; CHECK-NOT:   stf.spill
; CHECK:       (p6) br.cond.dpnt.many .LBB0_2
; CHECK-NOT:   ldf.fill
; CHECK:       br.ret.sptk.many b0
; CHECK:       .LBB0_2:
; CHECK:       stf.spill
; CHECK:       br.call.sptk.many b0 = g
; CHECK:       ldf.fill
; CSR-LABEL:   f:
; CSR:         stf.spill
; CSR:         (p6) br.cond.dpnt.many .LBB0_2

define double @f(double %a, double %b, double %c, double %x) {
entry:
  %r = call double @llvm.sqrt.f64(double %x)
  %ord = fcmp ord double %r, %r
  br i1 %ord, label %join, label %slow

slow:
  %s = call double @g(double %x)
  br label %join

join:
  %v = phi double [ %r, %entry ], [ %s, %slow ]
  %m = fmul double %v, %a
  %n = fadd double %m, %b
  %o = fmul double %n, %c
  ret double %o
}

declare double @llvm.sqrt.f64(double)
declare double @g(double)
