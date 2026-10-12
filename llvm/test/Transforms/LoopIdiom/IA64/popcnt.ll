; RUN: opt -passes=loop-idiom -mtriple=ia64-unknown-linux-gnu -S < %s \
; RUN:   | FileCheck %s

; A loop that clears the lowest set bit until none is left has counted the
; bits, which one instruction does.

define i32 @count(i64 %a) {
; CHECK-LABEL: @count(
; CHECK:         [[N:%.*]] = call i64 @llvm.ctpop.i64(i64 %a)
; CHECK-NEXT:    [[T:%.*]] = trunc i64 [[N]] to i32
entry:
  %none = icmp eq i64 %a, 0
  br i1 %none, label %done, label %body

body:
  %c = phi i32 [ %inc, %body ], [ 0, %entry ]
  %x = phi i64 [ %and, %body ], [ %a, %entry ]
  %inc = add nsw i32 %c, 1
  %sub = add i64 %x, -1
  %and = and i64 %sub, %x
  %last = icmp eq i64 %and, 0
  br i1 %last, label %done, label %body

done:
  %r = phi i32 [ 0, %entry ], [ %inc, %body ]
  ret i32 %r
}
