; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-counted-loops=false < %s | FileCheck %s

; A loop that reads and writes at fixed distances from where it has got to
; keeps a pointer for each of them, which the load or the store moves on,
; where one index would have to be added to the base for every one.

define void @schedule(ptr %w) {
; CHECK-LABEL: schedule:
; CHECK:       adds r14 = 64, r32
; CHECK-NEXT:  adds r15 = 4, r32
; CHECK-NEXT:  adds r16 = 36, r32
; CHECK-NEXT:  adds r17 = 56, r32
; CHECK-NEXT:  mov r18 = 48 ;;
; CHECK-NEXT:  .LBB0_1:
; CHECK:       ld4 r19 = [r17], 4
; CHECK-NEXT:  adds r18 = -1, r18
; CHECK-NEXT:  ld4 r20 = [r16], 4
; CHECK-NEXT:  ld4 r21 = [r15], 4 ;;
; CHECK-NEXT:  ld4 r22 = [r32], 4
; CHECK-NEXT:  add r19 = r20, r19 ;;
; CHECK-NEXT:  add r20 = r21, r22 ;;
; CHECK-NEXT:  add r19 = r19, r20 ;;
; CHECK-NEXT:  st4 [r14] = r19, 4
; CHECK-NEXT:  cmp.eq p7, p6 = 0, r18
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_1 ;;
entry:
  br label %loop

loop:
  %i = phi i64 [ 16, %entry ], [ %next, %loop ]
  %p = getelementptr [4 x i8], ptr %w, i64 %i
  %p2 = getelementptr i8, ptr %p, i64 -8
  %a = load i32, ptr %p2, align 4
  %p7 = getelementptr i8, ptr %p, i64 -28
  %b = load i32, ptr %p7, align 4
  %ab = add i32 %b, %a
  %p15 = getelementptr i8, ptr %p, i64 -60
  %c = load i32, ptr %p15, align 4
  %abc = add i32 %ab, %c
  %p16 = getelementptr i8, ptr %p, i64 -64
  %d = load i32, ptr %p16, align 4
  %abcd = add i32 %abc, %d
  store i32 %abcd, ptr %p, align 4
  %next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %next, 64
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; The index is kept if the loop ends on it, and the pointer is still
; separate.

define void @stride(ptr %d, i64 %n, i8 %c) {
; CHECK-LABEL: stride:
; CHECK:       .LBB1_2:
; CHECK:       adds r14 = 2, r14
; CHECK-NEXT:  st1 [r32] = r34, 2 ;;
; CHECK-NEXT:  cmp.lt p6, p7 = r14, r33
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB1_2 ;;
entry:
  %some = icmp sgt i64 %n, 0
  br i1 %some, label %loop, label %exit

loop:
  %i = phi i64 [ %next, %loop ], [ 0, %entry ]
  %p = getelementptr inbounds nuw i8, ptr %d, i64 %i
  store i8 %c, ptr %p, align 1
  %next = add nuw nsw i64 %i, 2
  %more = icmp slt i64 %next, %n
  br i1 %more, label %loop, label %exit

exit:
  ret void
}
