; RUN: llc -mtriple=ia64 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64 -verify-machineinstrs -ia64-if-conversion-limit=0 < %s \
; RUN:   | FileCheck %s --check-prefix=OFF

; A branch around a few instructions is a compare and its predicate on them.

declare void @callee()

define void @triangle(i64 %a, ptr %p) {
; CHECK-LABEL: triangle:
; CHECK:       cmp.eq p7, p6 = 0, r32 ;;
; CHECK-NEXT:  (p7) mov r14 = 1 ;;
; CHECK-NEXT:  (p7) st8 [r33] = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
;
; OFF-LABEL: triangle:
; OFF:       br.cond
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %join
then:
  store i64 1, ptr %p
  br label %join
join:
  ret void
}

; The side that the branch goes to.

define void @triangle_taken(i64 %a, ptr %p) {
; CHECK-LABEL: triangle_taken:
; CHECK:       cmp.eq p6, p7 = 0, r32 ;;
; CHECK-NEXT:  (p7) mov r14 = 1 ;;
; CHECK-NEXT:  (p7) st8 [r33] = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, 0
  br i1 %c, label %join, label %then
then:
  store i64 1, ptr %p
  br label %join
join:
  ret void
}

; Each side has one of the two predicates that the compare sets.

define void @diamond(i64 %a, ptr %p, ptr %q) {
; CHECK-LABEL: diamond:
; CHECK:       mov r35 = r1
; CHECK-NEXT:  .save rp, r37
; CHECK-NEXT:  mov r37 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  cmp.lt p6, p7 = -1, r32 ;;
; CHECK-NEXT:  (p6) mov r14 = 2 ;;
; CHECK-NEXT:  (p6) st8 [r34] = r14
; CHECK-NEXT:  (p7) mov r14 = 1 ;;
; CHECK-NEXT:  (p7) st8 [r33] = r14
; CHECK-NEXT:  br.call.sptk.many b0 = callee ;;
  %c = icmp slt i64 %a, 0
  br i1 %c, label %then, label %else
then:
  store i64 1, ptr %p
  br label %join
else:
  store i64 2, ptr %q
  br label %join
join:
  call void @callee()
  ret void
}

; Two sides that return the same way have one return between them.

define void @returns(i64 %a, ptr %p, ptr %q) {
; CHECK-LABEL: returns:
; CHECK:       cmp.lt p6, p7 = -1, r32 ;;
; CHECK-NEXT:  (p6) mov r14 = 2 ;;
; CHECK-NEXT:  (p6) st8 [r34] = r14
; CHECK-NEXT:  (p7) mov r14 = 1 ;;
; CHECK-NEXT:  (p7) st8 [r33] = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp slt i64 %a, 0
  br i1 %c, label %then, label %else
then:
  store i64 1, ptr %p
  ret void
else:
  store i64 2, ptr %q
  ret void
}

; A register that a side writes has what it had if the side is not taken.

define i64 @keeps(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: keeps:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  cmp.ltu p7, p6 = r8, r33 ;;
; CHECK-NEXT:  (p7) ld8 r14 = [r34] ;;
; CHECK-NEXT:  (p7) add r8 = r14, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp ult i64 %a, %b
  br i1 %c, label %then, label %join
then:
  %v = load i64, ptr %p
  %w = add i64 %v, %b
  br label %join
join:
  %r = phi i64 [ %a, %0 ], [ %w, %then ]
  ret i64 %r
}

; Floating-point compares set the two predicates as well.

define double @fp(double %a, double %b, ptr %p) {
; CHECK-LABEL: fp:
; CHECK:       fcmp.lt p7, p6 = f8, f9 ;;
; CHECK-NEXT:  (p7) ldfd f6 = [r34] ;;
; CHECK-NEXT:  (p7) fma.d f8 = f6, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = fcmp olt double %a, %b
  br i1 %c, label %then, label %join
then:
  %v = load double, ptr %p
  %w = fadd double %v, %b
  br label %join
join:
  %r = phi double [ %a, %0 ], [ %w, %then ]
  ret double %r
}

; A side that returns does so under the predicate, and the block goes on to
; the other side if it does not.

define i64 @early(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: early:
; CHECK:       cmp.eq p7, p6 = 0, r32 ;;
; CHECK-NEXT:  (p7) mov r8 = r33
; CHECK-NEXT:  (p7) br.ret.dpnt.many b0 ;;
; CHECK-NEXT:  {{.LBB[0-9_]+}}:
; CHECK-NEXT:  mov r14 = 1
;
; OFF-LABEL: early:
; OFF:       br.cond
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %else
then:
  ret i64 %b
else:
  store volatile i64 1, ptr %p
  store volatile i64 2, ptr %p
  store volatile i64 3, ptr %p
  ret i64 %a
}

; A return that is seldom reached stays behind its branch.

define i64 @early_cold(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: early_cold:
; CHECK:       br.cond.dpnt
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %else, !prof !0
then:
  ret i64 %b
else:
  store volatile i64 1, ptr %p
  store volatile i64 2, ptr %p
  store volatile i64 3, ptr %p
  ret i64 %a
}

; The instructions of a converted branch have a predicate, so the branch
; further up stays.

define void @nested(i64 %a, i64 %b, ptr %p) {
; CHECK-LABEL: nested:
; CHECK:       br.cond
; CHECK-NOT:   br.cond
; CHECK:       br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, 0
  br i1 %c, label %outer, label %join
outer:
  %d = icmp eq i64 %b, 0
  br i1 %d, label %inner, label %join
inner:
  store i64 1, ptr %p
  br label %join
join:
  ret void
}

; A call is more than its instruction.

define void @call(i64 %a) {
; CHECK-LABEL: call:
; CHECK:       br.cond
; CHECK:       br.call.sptk.many b0 = callee
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %join
then:
  call void @callee()
  br label %join
join:
  ret void
}

; More instructions than are worth going through for nothing.

define void @long(i64 %a, ptr %p) {
; CHECK-LABEL: long:
; CHECK:       br.cond
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %join
then:
  store volatile i64 1, ptr %p
  store volatile i64 2, ptr %p
  store volatile i64 3, ptr %p
  store volatile i64 4, ptr %p
  br label %join
join:
  ret void
}

; A side that is seldom taken.

define void @cold(i64 %a, ptr %p) {
; CHECK-LABEL: cold:
; CHECK:       br.cond.dpnt
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %join, !prof !0
then:
  store i64 1, ptr %p
  br label %join
join:
  ret void
}

; Nothing is converted without optimization.

define void @noopt(i64 %a, ptr %p) optnone noinline {
; CHECK-LABEL: noopt:
; CHECK:       br.cond
  %c = icmp eq i64 %a, 0
  br i1 %c, label %then, label %join
then:
  store i64 1, ptr %p
  br label %join
join:
  ret void
}

!0 = !{!"branch_weights", i32 1, i32 1000}
