; RUN: opt -mtriple=ia64-unknown-linux-gnu -mcpu=madison \
; RUN:   -passes=ia64-widen-indices -S < %s | FileCheck %s
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=ia64-widen-indices -S < %s \
; RUN:   | FileCheck %s --check-prefix=GENERIC

; Only a Madison versions loops: the copies made an Itanium 9340 slower.
; GENERIC-NOT: .wraps

; An unsigned 32-bit index of an innermost loop that nothing says does not
; wrap: the loop is versioned on it not wrapping over the trip count, and the
; loop that runs when it does not indexes with a 64-bit recurrence.

; CHECK-LABEL: @row(
; CHECK:       loop.preheader:
; CHECK:         br i1 %{{.*}}, label %loop.ph.wraps, label %loop.ph
; CHECK:       loop.wraps:
; CHECK:         %idx.wraps = add i32 %base, %i32.wraps
; CHECK-NEXT:    %idx64.wraps = zext i32 %idx.wraps to i64
; CHECK-NEXT:    %p.wraps = getelementptr inbounds i16, ptr %a, i64 %idx64.wraps
; CHECK:         br i1 %done.wraps, {{.*}} !llvm.loop [[SLOW:![0-9]+]]
; CHECK:       loop.ph:
; CHECK-NEXT:    [[START:%.*]] = zext i32 %base to i64
; CHECK:       loop:
; CHECK-NEXT:    %widen.iv = phi i64 [ [[START]], %loop.ph ], [ %widen.iv.next, %loop ]
; CHECK-NOT:     zext
; CHECK:         %p = getelementptr inbounds i16, ptr %a, i64 %widen.iv
; CHECK:         %widen.iv.next = add i64 %widen.iv, 1
; CHECK-NEXT:    br i1 %done, {{.*}} !llvm.loop [[FAST:![0-9]+]]
; CHECK:       {{^}}exit.loopexit:
; CHECK-NEXT:    phi i32 [ %s.next.lcssa.ph, %exit.loopexit.loopexit ], [ %s.next.lcssa.ph2, %exit.loopexit.loopexit1 ]
define i32 @row(ptr %a, i32 %base, i64 %n) {
entry:
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %i32 = trunc nuw i64 %i to i32
  %idx = add i32 %base, %i32
  %idx64 = zext i32 %idx to i64
  %p = getelementptr inbounds i16, ptr %a, i64 %idx64
  %v = load i16, ptr %p, align 2
  %v32 = sext i16 %v to i32
  %s.next = add i32 %s, %v32
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}

; An index whose recurrence cannot wrap is left alone.

; CHECK-LABEL: @nowrap(
; CHECK-NOT:   .wraps
; CHECK:       ret void
define void @nowrap(ptr %a, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %i64 = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr %a, i64 %i64
  store i32 0, ptr %p, align 4
  %i.next = add nuw i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; Without a trip count there is nothing to check the index against.

; CHECK-LABEL: @nocount(
; CHECK-NOT:   .wraps
; CHECK:       zext i32 %idx to i64
; CHECK-NOT:   .wraps
; CHECK:       ret void
define void @nocount(ptr %a, i32 %base) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %idx = add i32 %base, %i
  %idx64 = zext i32 %idx to i64
  %p = getelementptr inbounds i32, ptr %a, i64 %idx64
  %v = load volatile i32, ptr %p, align 4
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %v, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; A loop known to run only a few times saves less than its check costs: here
; the bound holds on each path into the phi, which SCEV does not see.

; CHECK-LABEL: @short(
; CHECK-NOT:   .wraps
; CHECK:       zext i32 %idx to i64
; CHECK-NOT:   .wraps
; CHECK:       ret i32
define i32 @short(ptr %a, i32 %base, i64 %x, i1 %c) {
entry:
  br i1 %c, label %left, label %right

left:
  %small = icmp ult i64 %x, 4
  br i1 %small, label %pre, label %out

right:
  %y = and i64 %x, 3
  br label %pre

pre:
  %n = phi i64 [ %x, %left ], [ %y, %right ]
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %out, label %loop

loop:
  %i = phi i64 [ 0, %pre ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s.next, %loop ]
  %i32 = trunc nuw i64 %i to i32
  %idx = add i32 %base, %i32
  %idx64 = zext i32 %idx to i64
  %p = getelementptr inbounds i16, ptr %a, i64 %idx64
  %v = load i16, ptr %p, align 2
  %v32 = sext i16 %v to i32
  %s.next = add i32 %s, %v32
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %out, label %loop

out:
  %r = phi i32 [ 0, %left ], [ 0, %pre ], [ %s.next, %loop ]
  ret i32 %r
}

; The checks of an inner loop whose index starts from the outer loop's
; induction variable are lifted out of the outer loop: the index is largest,
; m - 1 + n - 1, at the last iteration of each, and the whole nest is
; versioned once.

; CHECK-LABEL: @nest(
; CHECK:       outer.preheader:
; CHECK-NEXT:    [[M1:%.*]] = add i64 %m, -1
; CHECK-NEXT:    [[C1:%.*]] = icmp ugt i64 [[M1]], 4294967295
; CHECK-NEXT:    [[N1:%.*]] = add i64 %n, -1
; CHECK-NEXT:    [[C2:%.*]] = icmp ugt i64 [[N1]], 4294967295
; CHECK-NEXT:    [[O1:%.*]] = or i1 [[C1]], [[C2]]
; CHECK-NEXT:    [[MN:%.*]] = add i64 %n, %m
; CHECK-NEXT:    [[MN2:%.*]] = add i64 [[MN]], -2
; CHECK-NEXT:    [[C3:%.*]] = icmp ugt i64 [[MN2]], 4294967295
; CHECK-NEXT:    [[O2:%.*]] = or i1 [[O1]], [[C3]]
; CHECK-NEXT:    br i1 [[O2]], label %outer.ph.wraps, label %outer.ph
; CHECK:       inner.wraps:
; CHECK:         %idx64.wraps = zext i32 %idx.wraps to i64
; CHECK:       {{^}}outer:
; CHECK-NOT:   {{^}}inner.ph.wraps:
; CHECK:       {{^}}inner:
; CHECK-NOT:     zext
; CHECK:         br i1 %jd,
define void @nest(ptr %a, i64 %m, i64 %n) {
entry:
  %em = icmp eq i64 %m, 0
  %en = icmp eq i64 %n, 0
  %e = or i1 %em, %en
  br i1 %e, label %exit, label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %outer.latch ]
  %i32 = trunc nuw i64 %i to i32
  br label %inner

inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %j32 = trunc nuw i64 %j to i32
  %idx = add i32 %i32, %j32
  %idx64 = zext i32 %idx to i64
  %p = getelementptr inbounds i32, ptr %a, i64 %idx64
  store i32 0, ptr %p, align 4
  %j.next = add nuw nsw i64 %j, 1
  %jd = icmp eq i64 %j.next, %n
  br i1 %jd, label %outer.latch, label %inner

outer.latch:
  %i.next = add nuw nsw i64 %i, 1
  %id = icmp eq i64 %i.next, %m
  br i1 %id, label %exit, label %outer

exit:
  ret void
}

; CHECK-DAG: [[SLOW]] = distinct !{[[SLOW]], [[DONE:![0-9]+]], [[NOUNROLL:![0-9]+]]}
; CHECK-DAG: [[DONE]] = !{!"llvm.loop.ia64.widen_indices.done"}
; CHECK-DAG: [[NOUNROLL]] = !{!"llvm.loop.unroll.disable"}
; CHECK-DAG: [[FAST]] = distinct !{[[FAST]], [[DONE]]}
