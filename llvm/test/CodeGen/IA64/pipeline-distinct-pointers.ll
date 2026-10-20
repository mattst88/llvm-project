; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs -disable-lsr \
; RUN:   -pass-remarks-analysis=pipeliner -o /dev/null < %s 2>&1 | FileCheck %s

; Loop strength reduction may give each of a[i] and a[i + 1] its own pointer.
; The pipeliner relates pointers that step together from addresses a constant
; apart, through the phis of the loop and of the loop around it.

; a[2 * i] = a[2 * i + 1] * k never touches what a later iteration reads.
; CHECK: Schedule found with Initiation Interval: 3, MaxStageCount: 3
define void @shift(ptr %a, double %k, i64 %n) {
entry:
  %a1 = getelementptr i8, ptr %a, i64 8
  br label %loop
loop:
  %p = phi ptr [ %a, %entry ], [ %p.next, %loop ]
  %q = phi ptr [ %a1, %entry ], [ %q.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %v = load double, ptr %q
  %m = fmul double %v, %k
  store double %m, ptr %p
  %p.next = getelementptr i8, ptr %p, i64 16
  %q.next = getelementptr i8, ptr %q, i64 16
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; a[i + 1] = a[i] * k reads what the iteration before wrote.
; CHECK: Schedule found with Initiation Interval: 11, MaxStageCount: 0
define void @recurrence(ptr %a, double %k, i64 %n) {
entry:
  %a1 = getelementptr i8, ptr %a, i64 8
  br label %loop
loop:
  %p = phi ptr [ %a, %entry ], [ %p.next, %loop ]
  %q = phi ptr [ %a1, %entry ], [ %q.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %v = load double, ptr %p
  %m = fmul double %v, %k
  store double %m, ptr %q
  %p.next = getelementptr i8, ptr %p, i64 8
  %q.next = getelementptr i8, ptr %q, i64 8
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; Rows 2 * i and 2 * i + 1 of each column of a scaled by k, with a pointer to
; each row that steps by the column stride s in the outer loop.
; CHECK: Schedule found with Initiation Interval: 3, MaxStageCount: 3
define void @scale(ptr %a, float %k, i64 %s, i64 %n, i64 %m) {
entry:
  %a1 = getelementptr i8, ptr %a, i64 4
  br label %outer
outer:
  %r0 = phi ptr [ %a, %entry ], [ %r0.next, %outer.latch ]
  %r1 = phi ptr [ %a1, %entry ], [ %r1.next, %outer.latch ]
  %j = phi i64 [ 0, %entry ], [ %j.next, %outer.latch ]
  br label %loop
loop:
  %p0 = phi ptr [ %r0, %outer ], [ %p0.next, %loop ]
  %p1 = phi ptr [ %r1, %outer ], [ %p1.next, %loop ]
  %i = phi i64 [ 0, %outer ], [ %i.next, %loop ]
  %v0 = load float, ptr %p0
  %m0 = fmul float %v0, %k
  store float %m0, ptr %p0
  %v1 = load float, ptr %p1
  %m1 = fmul float %v1, %k
  store float %m1, ptr %p1
  %p0.next = getelementptr i8, ptr %p0, i64 8
  %p1.next = getelementptr i8, ptr %p1, i64 8
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %outer.latch, label %loop
outer.latch:
  %r0.next = getelementptr i8, ptr %r0, i64 %s
  %r1.next = getelementptr i8, ptr %r1, i64 %s
  %j.next = add nuw i64 %j, 1
  %odone = icmp eq i64 %j.next, %m
  br i1 %odone, label %exit, label %outer
exit:
  ret void
}


; y[3 * i + j] += x[360 * i + 120 * j] * k for j < 3, with a pointer to each of
; y[3 * i] and y[3 * i + 2]. The update of y[3 * i + 1] does not wait for the
; store to y[3 * i + 2], nor the load of y[3 * i + 2] for the store to
; y[3 * i + 1].
; CHECK: Schedule found with Initiation Interval: 6, MaxStageCount: 3
define void @axpy3(ptr noalias %y, ptr noalias %x, double %k, i64 %n) {
entry:
  %y2 = getelementptr i8, ptr %y, i64 16
  br label %loop
loop:
  %p0 = phi ptr [ %y, %entry ], [ %p0.next, %loop ]
  %p2 = phi ptr [ %y2, %entry ], [ %p2.next, %loop ]
  %px = phi ptr [ %x, %entry ], [ %px.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %p1 = getelementptr i8, ptr %p0, i64 8
  %px1 = getelementptr i8, ptr %px, i64 960
  %px2 = getelementptr i8, ptr %px, i64 1920
  %x0 = load double, ptr %px
  %m0 = fmul double %x0, %k
  %v0 = load double, ptr %p0
  %a0 = fadd double %v0, %m0
  store double %a0, ptr %p0
  %x1 = load double, ptr %px1
  %m1 = fmul double %x1, %k
  %v1 = load double, ptr %p1
  %a1 = fadd double %v1, %m1
  store double %a1, ptr %p1
  %x2 = load double, ptr %px2
  %m2 = fmul double %x2, %k
  %v2 = load double, ptr %p2
  %a2 = fadd double %v2, %m2
  store double %a2, ptr %p2
  %p0.next = getelementptr i8, ptr %p0, i64 24
  %p2.next = getelementptr i8, ptr %p2, i64 24
  %px.next = getelementptr i8, ptr %px, i64 2880
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
