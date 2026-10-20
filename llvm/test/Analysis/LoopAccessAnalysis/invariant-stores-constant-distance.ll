; RUN: opt -passes='print<access-info>' -disable-output < %s 2>&1 | FileCheck %s

; Two stores to loop-invariant addresses a constant distance apart write the
; same bytes in every iteration or in none, even if the addresses could wrap.
; Only the strided loads need checking against them.

define void @disjoint(ptr %s, ptr %p, i64 %n) {
; CHECK-LABEL: 'disjoint'
; CHECK-NEXT:    loop:
; CHECK-NEXT:      Memory dependences are safe with run-time checks
; CHECK-NEXT:      Dependences:
; CHECK-NEXT:      Run-time memory checks:
entry:
  %a = getelementptr i8, ptr %s, i64 8
  %b = getelementptr i8, ptr %s, i64 32
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %q = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %q
  store i64 %x, ptr %a
  store i64 %x, ptr %b
  %i.next = add nuw nsw i64 %i, 1
  %c = icmp eq i64 %i.next, %n
  br i1 %c, label %exit, label %loop

exit:
  ret void
}

; Or below it.
define void @disjoint_below(ptr %s, ptr %p, i64 %n) {
; CHECK-LABEL: 'disjoint_below'
; CHECK-NEXT:    loop:
; CHECK-NEXT:      Memory dependences are safe with run-time checks
; CHECK-NEXT:      Dependences:
; CHECK-NEXT:      Run-time memory checks:
entry:
  %a = getelementptr i8, ptr %s, i64 8
  %b = getelementptr i8, ptr %s, i64 -16
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %q = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %q
  store i64 %x, ptr %a
  store i64 %x, ptr %b
  %i.next = add nuw nsw i64 %i, 1
  %c = icmp eq i64 %i.next, %n
  br i1 %c, label %exit, label %loop

exit:
  ret void
}

; A store that ends past the start of the other still overlaps it.
define void @overlap(ptr %s, ptr %p, i64 %n) {
; CHECK-LABEL: 'overlap'
; CHECK-NEXT:    loop:
; CHECK-NEXT:      Report: unsafe dependent memory operations in loop.
; CHECK:           Unknown data dependence.
; CHECK-NEXT:      Dependences:
; CHECK-NEXT:        Unknown:
; CHECK-NEXT:            store i64 %x, ptr %a, align 4 ->
; CHECK-NEXT:            store i32 %y, ptr %b, align 4
entry:
  %a = getelementptr i8, ptr %s, i64 8
  %b = getelementptr i8, ptr %s, i64 12
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %q = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %q
  store i64 %x, ptr %a
  %y = trunc i64 %x to i32
  store i32 %y, ptr %b
  %i.next = add nuw nsw i64 %i, 1
  %c = icmp eq i64 %i.next, %n
  br i1 %c, label %exit, label %loop

exit:
  ret void
}
