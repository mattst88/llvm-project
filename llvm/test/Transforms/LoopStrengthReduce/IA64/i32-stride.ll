; RUN: opt -S -passes=loop-reduce < %s | FileCheck %s

; 32-bit integers are native to IA-64: cmp4, addp4, shladdp4, ld4, sxt4 and
; zxt4 take them as they are. With i32 a legal integer in the target's data
; layout, a 32-bit index that steps by a loop-invariant stride, like k * n + j
; in a column walk of a matrix, becomes an induction variable of its own
; rather than a multiply each iteration.
; CHECK-LABEL: @column(
; CHECK:       loop:
; CHECK-NOT:     mul
; CHECK:         add i32 %lsr.iv{{[0-9]*}}, %n
; CHECK-NOT:     mul
; CHECK:       exit:

target triple = "ia64-unknown-linux-gnu"

define i32 @column(ptr %b, i32 %n, i32 %j, i64 %count) {
entry:
  br label %loop
loop:
  %k = phi i64 [ 0, %entry ], [ %k.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %k32 = trunc nuw i64 %k to i32
  %row = mul i32 %n, %k32
  %idx = add i32 %row, %j
  %idx64 = zext i32 %idx to i64
  %p = getelementptr inbounds nuw [2 x i8], ptr %b, i64 %idx64
  %v = load i16, ptr %p, align 2
  %v32 = sext i16 %v to i32
  %sum.next = add nsw i32 %sum, %v32
  %k.next = add nuw nsw i64 %k, 1
  %c = icmp eq i64 %k.next, %count
  br i1 %c, label %exit, label %loop
exit:
  ret i32 %sum.next
}
