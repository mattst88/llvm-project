; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-loop-prefetch=false < %s | FileCheck %s --check-prefix=OFF

; A loop that loads a line or more apart prefetches the lines it will load
; some iterations later.

define i64 @column(ptr %a) {
; CHECK-LABEL: column:
; CHECK:       .LBB0_1:
; CHECK:         lfetch [r{{[0-9]+}}]
; CHECK:         br.cloop
; OFF-LABEL:   column:
; OFF-NOT:       lfetch
; OFF:           br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr [128 x i64], ptr %a, i64 %i, i64 0
  %v = load i64, ptr %p
  %sum.next = add i64 %sum, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 256
  br i1 %done, label %exit, label %loop

exit:
  ret i64 %sum.next
}
