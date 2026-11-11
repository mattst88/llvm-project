; RUN: opt -mtriple=ia64-unknown-linux-gnu -mcpu=madison -passes=loop-vectorize \
; RUN:   -S < %s | FileCheck %s
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=loop-vectorize -S < %s \
; RUN:   | FileCheck %s --check-prefix=GENERIC

; Only a Madison interleaves: two copies made an Itanium 9340 slower.
; GENERIC-NOT: vector.memcheck:
; GENERIC-NOT: vector.body:

; A loop that stores a double is interleaved by two behind a run-time check
; that its arrays do not overlap.

; CHECK-LABEL: @daxpy(
; CHECK:       vector.memcheck:
; CHECK:       vector.body:
; CHECK-COUNT-2: fmul double
; CHECK-COUNT-2: store double
; CHECK:       scalar.ph:
define void @daxpy(ptr %y, ptr %x, double %a, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %v = load double, ptr %px, align 8
  %m = fmul double %v, %a
  %py = getelementptr inbounds double, ptr %y, i64 %i
  %w = load double, ptr %py, align 8
  %s = fadd double %m, %w
  store double %s, ptr %py, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; A reduction stores nothing and is left to the pipeliner.

; CHECK-LABEL: @sum(
; CHECK-NOT:   vector.body:
; CHECK:       ret double
define double @sum(ptr %x, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %r = phi double [ 0.0, %entry ], [ %r.next, %loop ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %v = load double, ptr %px, align 8
  %r.next = fadd fast double %r, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret double %r.next
}

; An integer loop is not interleaved either.

; CHECK-LABEL: @fill(
; CHECK-NOT:   vector.body:
; CHECK:       ret void
define void @fill(ptr %y, i64 %v, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %py = getelementptr inbounds i64, ptr %y, i64 %i
  store i64 %v, ptr %py, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
