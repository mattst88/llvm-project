; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An and or or of compares is computed in the predicates: an ordinary compare
; writes p6 and p7, and parallel compares (or.andcm) after it each set or
; clear them only for one outcome, so they need no stops between them.

define i64 @set_both(i64 %a, i64 %b, i64 %c, i64 %d) {
; Both conditions: the second clears p6 (and sets p7) if it is false.
; CHECK-LABEL: set_both:
; CHECK:       cmp.eq p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r34, r35 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp eq i64 %c, %d
  %and = and i1 %c1, %c2
  %r = zext i1 %and to i64
  ret i64 %r
}

define i64 @set_either(i64 %a, i64 %b, i64 %c) {
; Either condition, the second a sign test, which compares zero with the
; register.
; CHECK-LABEL: set_either:
; CHECK:       cmp.eq p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  cmp.gt.or.andcm p6, p7 = r0, r34 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp slt i64 %c, 0
  %or = or i1 %c1, %c2
  %r = zext i1 %or to i64
  ret i64 %r
}

define i64 @set_all(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e) {
; A tree of ands is one chain. The compare with no parallel form goes first,
; and the rest share an instruction group.
; CHECK-LABEL: set_all:
; CHECK:       cmp.ltu p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  cmp.gt.or.andcm p7, p6 = r0, r35
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r36, r32
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = 3, r34 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
  %c1 = icmp ne i64 %c, 3
  %c2 = icmp ult i64 %a, %b
  %c3 = icmp sgt i64 %d, -1
  %c4 = icmp eq i64 %e, %a
  %and1 = and i1 %c1, %c2
  %and2 = and i1 %c3, %c4
  %and = and i1 %and1, %and2
  %r = zext i1 %and to i64
  ret i64 %r
}

define i64 @set_two_plain(i64 %a, i64 %b, i64 %c, i64 %d) {
; Only one compare can lack a parallel form.
; CHECK-LABEL: set_two_plain:
; CHECK-NOT:   or.andcm
; CHECK:       and r8 = r14, r15
  %c1 = icmp slt i64 %a, %b
  %c2 = icmp ult i64 %c, %d
  %and = and i1 %c1, %c2
  %r = zext i1 %and to i64
  ret i64 %r
}

define i64 @set_shared(i64 %a, i64 %b, i64 %c, i64 %d, ptr %p) {
; A compare whose result is also used elsewhere is not combined.
; CHECK-LABEL: set_shared:
; CHECK-NOT:   or.andcm
; CHECK:       and r8 = r14, r15
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp eq i64 %c, %d
  %s = zext i1 %c1 to i64
  store i64 %s, ptr %p
  %and = and i1 %c1, %c2
  %r = zext i1 %and to i64
  ret i64 %r
}

define i64 @set_fp(double %x, i64 %c) {
; FP compares have no parallel form and are not combined.
; CHECK-LABEL: set_fp:
; CHECK-NOT:   or.andcm
; CHECK:       or r8 = r14, r15
  %c1 = fcmp olt double %x, 0.0
  %c2 = icmp eq i64 %c, 0
  %or = or i1 %c1, %c2
  %r = zext i1 %or to i64
  ret i64 %r
}
