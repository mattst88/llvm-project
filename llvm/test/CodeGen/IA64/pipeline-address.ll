; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -pass-remarks-analysis=pipeliner -o /dev/null < %s 2>&1 | FileCheck %s

; The pipeliner tells how far apart the accesses of two iterations are from
; the base register of each and how much the loop steps it. That keeps the
; load of y[i + 1] from waiting for the store to y[i], and three iterations
; overlap.
define void @axpy(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
; CHECK: remark: <unknown>:0:0: Schedule found with Initiation Interval: 3, MaxStageCount: 3
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr double, ptr %x, i64 %i
  %py = getelementptr double, ptr %y, i64 %i
  %vx = load double, ptr %px
  %vy = load double, ptr %py
  %m = fmul contract double %a, %vx
  %r = fadd contract double %m, %vy
  store double %r, ptr %py
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; Each iteration swaps two 16-byte elements, one from the front and one from
; the back. The loads of one iteration need not wait for the stores of the
; iteration before, which are to the neighboring elements.
define void @reverse(ptr %from, ptr %to, i64 %n) {
; CHECK: remark: <unknown>:0:0: Schedule found with Initiation Interval: 4, MaxStageCount: 1
entry:
  %e = icmp eq i64 %n, 0
  br i1 %e, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %f = phi ptr [ %from, %entry ], [ %f.next, %loop ]
  %t = phi ptr [ %to, %entry ], [ %t.next, %loop ]
  %f8 = getelementptr i8, ptr %f, i64 8
  %t8 = getelementptr i8, ptr %t, i64 8
  %fv = load i64, ptr %f
  %ft = load i32, ptr %f8
  %tv = load i64, ptr %t
  store i64 %tv, ptr %f
  %tt = load i32, ptr %t8
  store i32 %tt, ptr %f8
  store i64 %fv, ptr %t
  store i32 %ft, ptr %t8
  %f.next = getelementptr i8, ptr %f, i64 16
  %t.next = getelementptr i8, ptr %t, i64 -16
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; The address of a stack slot is an adds from a frame index, which has no
; register to report the access against.
declare void @init(ptr)

define void @slot(ptr %p, i64 %n) {
; CHECK: remark: <unknown>:0:0: Schedule found with Initiation Interval: 3, MaxStageCount: 0
entry:
  %a = alloca [4 x i64], align 8
  call void @init(ptr %a)
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %s
  %v = load i64, ptr %a
  %w = add i64 %v, %x
  store i64 %w, ptr %a
  %i.next = add nuw nsw i64 %i, 1
  %c = icmp ult i64 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  call void @init(ptr %a)
  ret void
}
