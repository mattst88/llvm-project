; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s

; bzip2's unRLE chase, t = tt[t] >> 8. Every value of t comes from an ld4,
; so its upper half is already zero and the index needs no zxt4.
; CHECK-LABEL: chase:
; CHECK-NOT: zxt4
; CHECK: shladd [[A:r[0-9]+]] = {{r[0-9]+}}, 2,
; CHECK-NEXT: ld4 [[V:r[0-9]+]] = [[[A]]]
; CHECK-NOT: zxt4
; CHECK: br.cloop
define i32 @chase(ptr %tt, ptr %p, i64 %n) {
entry:
  %t.init = load i32, ptr %p, align 4
  br label %loop

loop:
  %t = phi i32 [ %t.init, %entry ], [ %t.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %idx = zext i32 %t to i64
  %a = getelementptr inbounds i32, ptr %tt, i64 %idx
  %v = load i32, ptr %a, align 4
  %t.next = lshr i32 %v, 8
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %t.next
}

; An add can carry into the upper half, so the zxt4 stays.
; CHECK-LABEL: carry:
; CHECK: zxt4
define i32 @carry(ptr %tt, ptr %p, i64 %n) {
entry:
  %t.init = load i32, ptr %p, align 4
  br label %loop

loop:
  %t = phi i32 [ %t.init, %entry ], [ %t.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %idx = zext i32 %t to i64
  %a = getelementptr inbounds i32, ptr %tt, i64 %idx
  %v = load i32, ptr %a, align 4
  %t.next = add i32 %v, 1
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %t.next
}
