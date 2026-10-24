; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s

; A byte loaded before the loop and on the back edge needs no zxt1 in the
; loop: both registers were written zero-extended.
; CHECK-LABEL: count_digits:
; CHECK:       ld1 [[C:r[0-9]+]] = [r32], 1
; CHECK:     .LBB0_1:
; CHECK-NOT:   zxt1
; CHECK:       adds {{r[0-9]+}} = -48, [[C]]
; CHECK-NOT:   zxt1
; CHECK:       br.cond.dptk.few .LBB0_1
define i32 @count_digits(ptr %p) {
entry:
  %first = load i8, ptr %p
  br label %loop

loop:
  %c = phi i8 [ %first, %entry ], [ %next, %latch ]
  %q = phi ptr [ %p, %entry ], [ %q.next, %latch ]
  %n = phi i32 [ 0, %entry ], [ %n.next, %latch ]
  %end = icmp eq i8 %c, 0
  br i1 %end, label %exit, label %body

body:
  %d = add i8 %c, -48
  %isdigit = icmp ult i8 %d, 10
  %inc = zext i1 %isdigit to i32
  %n.next = add i32 %n, %inc
  br label %latch

latch:
  %q.next = getelementptr i8, ptr %q, i64 1
  %next = load i8, ptr %q.next
  br label %loop

exit:
  ret i32 %n
}
