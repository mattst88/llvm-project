; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=loop-data-prefetch -S < %s \
; RUN:   | FileCheck %s

; A load that steps a line or more each iteration is prefetched as many
; iterations ahead as take about 100 instructions: here 16 of 6.

define i64 @column(ptr %a) {
; CHECK-LABEL: @column(
; CHECK:         [[OFF:%.*]] = add i64 %{{.*}}, 16384
; CHECK-NEXT:    [[AHEAD:%.*]] = getelementptr i8, ptr %a, i64 [[OFF]]
; CHECK:         call void @llvm.prefetch.p0(ptr [[AHEAD]], i32 0, i32 3, i32 1)
; CHECK-NEXT:    load i64
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

; One that steps less is not.

define i64 @row(ptr %a) {
; CHECK-LABEL: @row(
; CHECK-NOT:     call void @llvm.prefetch
; CHECK:         ret i64
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %sum.next = add i64 %sum, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 256
  br i1 %done, label %exit, label %loop

exit:
  ret i64 %sum.next
}
