; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-pipeliner=false < %s | FileCheck %s

; alloc and the save of ar.lc can only issue on I0, so the save waits until
; the body's first instructions, and the unwind directive follows it. Among
; them it takes the first I slot of the bundle, which is the one that goes
; to I0. The count is compared signed, so the compare needs it zero-extended
; in the entry block.
define i64 @count(ptr %a, i32 %i, i32 %n) {
; CHECK-LABEL: count:
; CHECK:       .save ar.pfs, r35
; CHECK-NEXT:  alloc r35 = ar.pfs, 3, 2, 0, 0
; CHECK-NEXT:  .save ar.lc, r36
; CHECK-NEXT:  mov r36 = ar.lc
; CHECK-NEXT:  .body
; CHECK-NEXT:  zxt4 [[N:r[0-9]+]] = r34
; CHECK-NEXT:  zxt4 {{r[0-9]+}} = r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  cmp.lt p7, p6 = 0, [[N]]
entry:
  %ie = zext i32 %i to i64
  %ne = zext i32 %n to i64
  %p0 = getelementptr i8, ptr %a, i64 %ie
  %z = icmp slt i64 %ne, 1
  br i1 %z, label %exit, label %loop
loop:
  %j = phi i64 [ 0, %entry ], [ %j.next, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr i8, ptr %p0, i64 %j
  %v = load i8, ptr %p
  %ve = zext i8 %v to i64
  %s.next = add i64 %s, %ve
  %j.next = add nuw i64 %j, 1
  %d = icmp eq i64 %j.next, %ne
  br i1 %d, label %exit, label %loop
exit:
  %r = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  ret i64 %r
}
