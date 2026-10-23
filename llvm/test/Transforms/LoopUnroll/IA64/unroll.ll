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

; A known trip count that the unroll factor would not divide leaves an exit
; inside the unrolled body, which the pipeliner cannot take, so such a loop
; stays rolled.
define void @odd_count(ptr %a, ptr %b) {
; CHECK-LABEL: @odd_count(
; CHECK-COUNT-1: load double
; CHECK-NOT:   load double
; CHECK-LABEL: }
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %w = fmul double %v, 5.0e-1
  %q = getelementptr double, ptr %b, i64 %i
  store double %w, ptr %q
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1001
  br i1 %done, label %exit, label %for.body
exit:
  ret void
}

; One that it divides still unrolls.
define void @even_count(ptr %a, ptr %b) {
; CHECK-LABEL: @even_count(
; CHECK:       for.body:
; CHECK-COUNT-4: load double
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %w = fmul double %v, 5.0e-1
  %q = getelementptr double, ptr %b, i64 %i
  store double %w, ptr %q
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1000
  br i1 %done, label %exit, label %for.body
exit:
  ret void
}

; A phi taking another phi's value each iteration, as SHA-1's rounds do
; with e = d; d = c; b = a, needs a copy.  The loop unrolls by more than
; the longest such chain, here d then e, at a count dividing its trip count,
; though the unrolled body is over the threshold.
declare i32 @llvm.fshl.i32(i32, i32, i32)
define i32 @rotate(ptr %w, i32 %a0, i32 %b0, i32 %c0, i32 %d0, i32 %e0) {
; CHECK-LABEL: @rotate(
; CHECK:       for.body:
; CHECK-COUNT-4: load i32
; CHECK-NOT:   load i32
; CHECK:       br i1
; OFF-LABEL:   @rotate(
; OFF-COUNT-1: load i32
; OFF-NOT:     load i32
entry:
  br label %for.body
for.body:
  %i = phi i64 [ 0, %entry ], [ %i.next, %for.body ]
  %a = phi i32 [ %a0, %entry ], [ %t, %for.body ]
  %b = phi i32 [ %b0, %entry ], [ %a, %for.body ]
  %c = phi i32 [ %c0, %entry ], [ %b.rot, %for.body ]
  %d = phi i32 [ %d0, %entry ], [ %c, %for.body ]
  %e = phi i32 [ %e0, %entry ], [ %d, %for.body ]
  %a.rot = call i32 @llvm.fshl.i32(i32 %a, i32 %a, i32 5)
  %bc = xor i32 %b, %c
  %f = xor i32 %bc, %d
  %p = getelementptr i32, ptr %w, i64 %i
  %v = load i32, ptr %p
  %t1 = add i32 %a.rot, %f
  %t2 = add i32 %t1, %e
  %t3 = add i32 %t2, %v
  %t = add i32 %t3, 1859775393
  %b.rot = call i32 @llvm.fshl.i32(i32 %b, i32 %b, i32 30)
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 20
  br i1 %done, label %exit, label %for.body
exit:
  %r1 = add i32 %t, %b.rot
  %r2 = add i32 %r1, %c
  %r3 = add i32 %r2, %d
  ret i32 %r3
}
