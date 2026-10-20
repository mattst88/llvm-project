; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-rotating-max-stages=0 -pass-remarks-analysis=pipeliner \
; RUN:   -o - < %s 2>&1 | FileCheck %s

; lfetch is marked as a store so that nothing deletes it, but its memory
; operand only loads. The pipeliner orders it as a load, so the load of b[i + 1]
; does not wait for the prefetch of the iteration before, which may alias it
; and comes after the store to b[i]. Three iterations overlap.
define void @sum(ptr %b, ptr %a, i64 %n) {
; CHECK: remark: <unknown>:0:0: Schedule found with Initiation Interval: 3, MaxStageCount: 3
; CHECK-LABEL: sum:
; CHECK: lfetch
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %pb = getelementptr double, ptr %b, i64 %i
  %vb = load double, ptr %pb
  %r = fadd double %vb, 1.0
  store double %r, ptr %pb
  %pa = getelementptr [120 x double], ptr %a, i64 %i
  call void @llvm.prefetch.p0(ptr %pa, i32 0, i32 3, i32 1)
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
