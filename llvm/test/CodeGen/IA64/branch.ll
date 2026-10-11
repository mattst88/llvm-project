; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A conditional branch is a compare and a branch under its predicate, which
; can share a group: a branch reads the predicate as it is written. The
; block that follows is the one the branch was to, so the condition is the
; opposite of the one written here.

define i64 @branch_eq(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: branch_eq:
; CHECK:       cmp.eq p7, p6 = r32, r33
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_2 ;;
; CHECK-NEXT:  // %bb.1:
; CHECK-NEXT:  st8 [r34] = r32
; CHECK-NEXT:  mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB0_2:
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, %b
  br i1 %c, label %then, label %else
then:
  store i64 %a, ptr %p
  ret i64 1
else:
  ret i64 0
}

define i64 @branch_constant(i64 %a, ptr %p) {
; CHECK-LABEL: branch_constant:
; CHECK:       cmp.lt p6, p7 = 9, r32
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB1_2 ;;
; CHECK-NEXT:  // %bb.1:
  %c = icmp slt i64 %a, 10
  br i1 %c, label %then, label %else
then:
  store i64 %a, ptr %p
  ret i64 1
else:
  ret i64 0
}

; A condition that is a value is compared with zero.

define i64 @branch_value(i1 %c, ptr %p) {
; CHECK-LABEL: branch_value:
; CHECK:       and [[C:r[0-9]+]] = 1, r32 ;;
; CHECK-NEXT:  cmp.eq p6, p7 = 0, [[C]]
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB2_2 ;;
; CHECK-NEXT:  // %bb.1:
  br i1 %c, label %then, label %else
then:
  store i64 1, ptr %p
  ret i64 1
else:
  ret i64 0
}

; A branch says whether it expects to be taken.

define i64 @loop(i64 %n, ptr %p) {
; CHECK-LABEL: loop:
; CHECK:       .LBB3_1:
; CHECK:       st8 [r33] = [[I:r[0-9]+]]
; CHECK:       cmp.eq p7, p6 = r32, [[I]]
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB3_1 ;;
; CHECK-NEXT:  // %bb.2:
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  br label %body
body:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  store volatile i64 %i, ptr %p
  %next = add i64 %i, 1
  %c = icmp eq i64 %next, %n
  br i1 %c, label %exit, label %body
exit:
  ret i64 %next
}

define void @unlikely(i64 %a, ptr %p) {
; CHECK-LABEL: unlikely:
; CHECK:       cmp.eq p7, p6 = 0, r32
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB4_2 ;;
; CHECK-NEXT:  // %bb.1:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %else, !prof !0
then:
  store i64 1, ptr %p
  br label %else
else:
  ret void
}

; A branch nothing qualifies.

define void @forever(ptr %p) {
; CHECK-LABEL: forever:
; CHECK:       .LBB5_1:
; CHECK:       st8 [r32] = r0
; CHECK-NEXT:  br.cond.sptk.few .LBB5_1 ;;
entry:
  br label %body
body:
  store volatile i64 0, ptr %p
  br label %body
}

!0 = !{!"branch_weights", i32 1000, i32 1}
