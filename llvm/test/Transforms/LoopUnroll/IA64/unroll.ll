; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu -passes=loop-unroll | FileCheck %s
; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu -passes=loop-unroll \
; RUN:   -ia64-unroll-threshold=0 | FileCheck %s --check-prefix=OFF

; A loop of unknown trip count unrolls four times, with a remainder loop
; for the iterations left over.
define i64 @sum(ptr %a, i64 %n) {
; CHECK-LABEL: @sum(
; CHECK:       for.body:
; CHECK-COUNT-4: load i64
; CHECK-NOT:   load i64
; CHECK:       br i1
; CHECK:       for.body.epil:
; OFF-LABEL:   @sum(
; OFF-COUNT-1: load i64
; OFF-NOT:     load i64
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %s.next = add i64 %s, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret i64 %s.next
}

; A call costs more than the loop overhead unrolling would save.
declare void @f(i64)
define void @call(i64 %n) {
; CHECK-LABEL: @call(
; OFF-LABEL:   @call(
; CHECK-COUNT-1: call void @f
; CHECK-NOT:   call void @f
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  call void @f(i64 %i)
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret void
}

; An intrinsic that is not a call does not stop unrolling.
declare i64 @llvm.ctpop.i64(i64)
define i64 @popcount(ptr %a, i64 %n) {
; CHECK-LABEL: @popcount(
; CHECK:       for.body:
; CHECK-COUNT-4: call i64 @llvm.ctpop.i64
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %c = call i64 @llvm.ctpop.i64(i64 %v)
  %s.next = add i64 %s, %c
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret i64 %s.next
}

; Nothing unrolls partially when optimizing for size.
define i64 @small(ptr %a, i64 %n) optsize {
; CHECK-LABEL: @small(
; CHECK-COUNT-1: load i64
; CHECK-NOT:   load i64
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %s.next = add i64 %s, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret i64 %s.next
}

; A loop that runs at most three times unrolls completely, each copy guarded
; by its own exit test.
define void @bounded(ptr %a, i64 %n) {
; CHECK-LABEL: @bounded(
; CHECK-COUNT-3: store i64
; CHECK-NOT:   for.body:
; CHECK-LABEL: }
entry:
  %m = and i64 %n, 3
  %z = icmp eq i64 %m, 0
  br i1 %z, label %exit, label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %p = getelementptr i64, ptr %a, i64 %i
  store i64 %i, ptr %p
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %m
  br i1 %done, label %exit, label %for.body
exit:
  ret void
}

; Unrolling cannot shorten an FP sum that may not be reassociated, so a loop
; of unknown trip count is left for the pipeliner.
define double @fsum(ptr %a, i64 %n) {
; CHECK-LABEL: @fsum(
; CHECK-COUNT-1: load double
; CHECK-NOT:   load double
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %s.next = fadd double %s, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret double %s.next
}

; A sum that may be reassociated still unrolls.
define double @fsum_reassoc(ptr %a, i64 %n) {
; CHECK-LABEL: @fsum_reassoc(
; CHECK:       for.body:
; CHECK-COUNT-4: load double
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %s.next = fadd reassoc nsz double %s, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret double %s.next
}

; A store in the loop can keep the pipeliner from overlapping its loads, so
; the sum still unrolls.
define void @fsum_store(ptr %a, ptr %d, i64 %n) {
; CHECK-LABEL: @fsum_store(
; CHECK:       for.body:
; CHECK-COUNT-4: load double
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %for.body ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %s.next = fadd double %s, %v
  store double %s.next, ptr %d
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %for.body
exit:
  ret void
}
