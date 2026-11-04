; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; The join after the check of the inline sqrt runs whenever the loop body
; does. The store's address is not sunk into it, where it would wait for
; the sqrt, but computed alongside it.
; CHECK-LABEL: f:
; CHECK:       .LBB0_1:
; CHECK:       sxt4 r14 = r36
; CHECK-NEXT:  shladd r37 = r14, 3, r33
; CHECK-NEXT:  frsqrta
; CHECK:       (p6) br.cond.dpnt.many .LBB0_3
; CHECK-NEXT:  // %bb.2: // %join
; CHECK-NEXT:  // in Loop: Header=BB0_1 Depth=1
; CHECK-NEXT:  add r36 = r36, r34
; CHECK-NEXT:  stfd [r37] = f8

define void @f(ptr %a, ptr %b, i32 %s, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %join ]
  %ip = getelementptr inbounds double, ptr %a, i32 %i
  %x = load double, ptr %ip
  %r = call double @llvm.sqrt.f64(double %x)
  %ord = fcmp ord double %r, %r
  %m = mul nsw i32 %i, %s
  %idx = sext i32 %m to i64
  %op = getelementptr inbounds double, ptr %b, i64 %idx
  br i1 %ord, label %join, label %slow

slow:
  %c = call double @g(double %x)
  br label %join

join:
  %v = phi double [ %r, %loop ], [ %c, %slow ]
  store double %v, ptr %op
  %inc = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

declare double @llvm.sqrt.f64(double)
declare double @g(double)
