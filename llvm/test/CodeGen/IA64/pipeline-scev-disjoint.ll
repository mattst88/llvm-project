; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-enable-postinc-pointers=false -ia64-rotating-max-stages=0 \
; RUN:   -pass-remarks-analysis=pipeliner -o /dev/null < %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-enable-postinc-pointers=false -ia64-rotating-max-stages=0 \
; RUN:   -pipeliner-scev-disjoint=false -pass-remarks-analysis=pipeliner \
; RUN:   -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=OFF

; The butterfly of an FFT pass, a[i] and a[i + d] for i stepping by 2 * d.
; Scalar evolution shows that for d > 0 the two never meet, in the same
; iteration or a later one.

; CHECK: Schedule found with Initiation Interval: 3,
; OFF: Schedule found with Initiation Interval: {{[0-9][0-9]}},
define void @butterfly(ptr %a, i32 %d, i64 %n) {
entry:
  %guard = icmp sgt i32 %d, 0
  br i1 %guard, label %loop.ph, label %exit
loop.ph:
  %step.32 = shl nuw nsw i32 %d, 1
  %step = zext i32 %step.32 to i64
  %d.ext = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %j = add nsw i64 %i, %d.ext
  %p = getelementptr inbounds double, ptr %a, i64 %i
  %q = getelementptr inbounds double, ptr %a, i64 %j
  %x = load double, ptr %p
  %y = load double, ptr %q
  %s = fadd double %x, %y
  %t = fsub double %x, %y
  store double %s, ptr %p
  store double %t, ptr %q
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}

; Without the guard, d may be 0, and a[i + d] the same as a[i].
; CHECK: Schedule found with Initiation Interval: {{[0-9][0-9]}},
define void @unguarded(ptr %a, i32 %d, i64 %n) {
entry:
  %step.32 = shl nuw nsw i32 %d, 1
  %step = zext i32 %step.32 to i64
  %d.ext = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %j = add nsw i64 %i, %d.ext
  %p = getelementptr inbounds double, ptr %a, i64 %i
  %q = getelementptr inbounds double, ptr %a, i64 %j
  %x = load double, ptr %p
  %y = load double, ptr %q
  %s = fadd double %x, %y
  %t = fsub double %x, %y
  store double %s, ptr %p
  store double %t, ptr %q
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}
