; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=loop-unroll-full -S < %s | FileCheck %s

; A short loop is fully unrolled past the generic threshold of 300.
; CHECK-LABEL: @short(
; CHECK-NOT: br i1
; CHECK: ret i64
define i64 @short(i64 %x) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ %x, %entry ], [ %v30, %loop ]
  %v0 = xor i64 %s, %i
  %v1 = add i64 %v0, %s
  %v2 = xor i64 %v1, %s
  %v3 = add i64 %v2, %s
  %v4 = xor i64 %v3, %s
  %v5 = add i64 %v4, %s
  %v6 = xor i64 %v5, %s
  %v7 = add i64 %v6, %s
  %v8 = xor i64 %v7, %s
  %v9 = add i64 %v8, %s
  %v10 = xor i64 %v9, %s
  %v11 = add i64 %v10, %s
  %v12 = xor i64 %v11, %s
  %v13 = add i64 %v12, %s
  %v14 = xor i64 %v13, %s
  %v15 = add i64 %v14, %s
  %v16 = xor i64 %v15, %s
  %v17 = add i64 %v16, %s
  %v18 = xor i64 %v17, %s
  %v19 = add i64 %v18, %s
  %v20 = xor i64 %v19, %s
  %v21 = add i64 %v20, %s
  %v22 = xor i64 %v21, %s
  %v23 = add i64 %v22, %s
  %v24 = xor i64 %v23, %s
  %v25 = add i64 %v24, %s
  %v26 = xor i64 %v25, %s
  %v27 = add i64 %v26, %s
  %v28 = xor i64 %v27, %s
  %v29 = add i64 %v28, %s
  %v30 = xor i64 %v29, %s
  %i.next = add i64 %i, 1
  %c = icmp eq i64 %i.next, 12
  br i1 %c, label %exit, label %loop

exit:
  ret i64 %v30
}

; A long one is left for the pipeliner.
; CHECK-LABEL: @long(
; CHECK: br i1 %c, label %exit, label %loop
define i64 @long(i64 %x) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ %x, %entry ], [ %v30, %loop ]
  %v0 = xor i64 %s, %i
  %v1 = add i64 %v0, %s
  %v2 = xor i64 %v1, %s
  %v3 = add i64 %v2, %s
  %v4 = xor i64 %v3, %s
  %v5 = add i64 %v4, %s
  %v6 = xor i64 %v5, %s
  %v7 = add i64 %v6, %s
  %v8 = xor i64 %v7, %s
  %v9 = add i64 %v8, %s
  %v10 = xor i64 %v9, %s
  %v11 = add i64 %v10, %s
  %v12 = xor i64 %v11, %s
  %v13 = add i64 %v12, %s
  %v14 = xor i64 %v13, %s
  %v15 = add i64 %v14, %s
  %v16 = xor i64 %v15, %s
  %v17 = add i64 %v16, %s
  %v18 = xor i64 %v17, %s
  %v19 = add i64 %v18, %s
  %v20 = xor i64 %v19, %s
  %v21 = add i64 %v20, %s
  %v22 = xor i64 %v21, %s
  %v23 = add i64 %v22, %s
  %v24 = xor i64 %v23, %s
  %v25 = add i64 %v24, %s
  %v26 = xor i64 %v25, %s
  %v27 = add i64 %v26, %s
  %v28 = xor i64 %v27, %s
  %v29 = add i64 %v28, %s
  %v30 = xor i64 %v29, %s
  %i.next = add i64 %i, 1
  %c = icmp eq i64 %i.next, 70
  br i1 %c, label %exit, label %loop

exit:
  ret i64 %v30
}
