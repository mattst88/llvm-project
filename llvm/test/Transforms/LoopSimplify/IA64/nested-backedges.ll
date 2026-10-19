; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=loop-simplify -S %s \
; RUN:   | FileCheck %s --check-prefix=IA64
; RUN: opt -passes=loop-simplify -S %s | FileCheck %s --check-prefix=DEFAULT

; The loop goes round through %swap, which bumps %lo, or through %skip, which
; keeps it. A target that splits that into an inner loop over %skip and an
; outer one over %swap sets the inner loop up again after every swap. IA64
; merges the two backedges instead.

; IA64-LABEL: define i32 @partition(
; IA64:       loop:
; IA64-NEXT:    %lo = phi i32 [ 0, %entry ], [ %lo.be, %loop.backedge ]
; IA64-NEXT:    %i = phi i32 [ 0, %entry ], [ %i.next, %loop.backedge ]
; IA64:       loop.backedge:
; IA64-NEXT:    %lo.be = phi i32 [ %lo.next, %swap ], [ %lo, %skip ]
; IA64-NOT:   .outer

; DEFAULT-LABEL: define i32 @partition(
; DEFAULT:       loop.outer:
; DEFAULT:       loop:
; DEFAULT-NEXT:    %i = phi i32

define i32 @partition(ptr %a, i32 %n, i32 %pivot) {
entry:
  br label %loop

loop:
  %lo = phi i32 [ 0, %entry ], [ %lo.next, %swap ], [ %lo, %skip ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %swap ], [ %i.next, %skip ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p, align 4
  %i.next = add nsw i32 %i, 1
  %lt = icmp slt i32 %v, %pivot
  br i1 %lt, label %swap, label %skip

swap:
  %q = getelementptr inbounds i32, ptr %a, i32 %lo
  %w = load i32, ptr %q, align 4
  store i32 %v, ptr %q, align 4
  store i32 %w, ptr %p, align 4
  %lo.next = add nsw i32 %lo, 1
  %done.s = icmp eq i32 %i.next, %n
  br i1 %done.s, label %exit, label %loop

skip:
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i32 [ %lo.next, %swap ], [ %lo, %skip ]
  ret i32 %r
}
