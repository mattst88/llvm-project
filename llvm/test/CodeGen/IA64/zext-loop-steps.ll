; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=ia64-zext-loop-steps -S < %s | FileCheck %s

; for (i = 0; i < n; i += 2 * d) cannot repeat with a negative step.
; CHECK-LABEL: @step
; CHECK: loop.ph:
; CHECK: %step.zext = zext i32 %step.32 to i64
; CHECK: %i.next = add nsw i64 %i, %step.zext
define void @step(ptr %a, i32 %d, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step.32 = shl nuw i32 %d, 1
  %step = sext i32 %step.32 to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  store double 0.0, ptr %p
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; The same with the test reversed.
; CHECK-LABEL: @swapped
; CHECK: %i.next = add nsw i64 %step.zext, %i
define void @swapped(ptr %a, i32 %d, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  store double 0.0, ptr %p
  %i.next = add nsw i64 %step, %i
  %done = icmp sle i64 %n, %i.next
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; A start that may be near the largest i64 may overflow with a zero-extended
; negative step.
; CHECK-LABEL: @any_start
; CHECK-NOT: zext
define void @any_start(ptr %a, i32 %d, i64 %s, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ %s, %loop.ph ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  store double 0.0, ptr %p
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; The final count is used after the loop.
; CHECK-LABEL: @used_after
; CHECK-NOT: zext
define i64 @used_after(ptr %a, i32 %d, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  store double 0.0, ptr %p
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  %r = phi i64 [ %i.next, %loop ]
  ret i64 %r
}

; A call may leave the loop before the count overflows.
; CHECK-LABEL: @call
; CHECK-NOT: zext
declare void @f()
define void @call(i32 %d, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  call void @f()
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; Without nsw the count may wrap past the bound.
; CHECK-LABEL: @wraps
; CHECK-NOT: zext
define void @wraps(ptr %a, i32 %d, i64 %n) {
entry:
  br label %loop.ph
loop.ph:
  %step = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  store double 0.0, ptr %p
  %i.next = add i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; The step 2 * d is poison for a negative d, so d is not negative wherever the
; loop runs.
; CHECK-LABEL: @shifted
; CHECK: %d.out = sext i32 %d to i64
; CHECK: %d.ext = zext nneg i32 %d to i64
define i64 @shifted(ptr %a, i32 %d, i64 %n) {
entry:
  %d.out = sext i32 %d to i64
  br label %loop.ph
loop.ph:
  %step.32 = shl nuw i32 %d, 1
  %step = sext i32 %step.32 to i64
  %d.ext = sext i32 %d to i64
  br label %loop
loop:
  %i = phi i64 [ 0, %loop.ph ], [ %i.next, %loop ]
  %j = add nsw i64 %i, %d.ext
  %p = getelementptr inbounds double, ptr %a, i64 %j
  store double 0.0, ptr %p
  %i.next = add nsw i64 %i, %step
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i64 %d.out
}
