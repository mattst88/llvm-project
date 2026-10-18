; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A select on an and or or of compares, which the DAG combiner turns into
; nested selects, tests the conditions with one compare and parallel compares
; after it, and selects once.

define i64 @sel_both(i64 %a, i64 %b, i64 %c, i64 %d, i64 %x, i64 %y) {
; CHECK-LABEL: sel_both:
; CHECK:       cmp.eq p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r37 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r34, r35 ;;
; CHECK-NEXT:  (p6) mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp eq i64 %c, %d
  %and = and i1 %c1, %c2
  %r = select i1 %and, i64 %x, i64 %y
  ret i64 %r
}

define double @sel_either(i64 %a, i64 %b, i64 %c, double %x, double %y) {
; Selecting between FP registers.
; CHECK-LABEL: sel_either:
; CHECK:       cmp.eq p6, p7 = r32, r33 ;;
; CHECK-NEXT:  cmp.lt.or.andcm p6, p7 = r0, r34 ;;
; CHECK-NEXT:  (p6) mov f9 = f8 ;;
; CHECK-NEXT:  mov f8 = f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp sgt i64 %c, 0
  %or = or i1 %c1, %c2
  %r = select i1 %or, double %x, double %y
  ret double %r
}

define i64 @sel_all(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %x, i64 %y) {
; The compare with no parallel form goes first.
; CHECK-LABEL: sel_all:
; CHECK:       cmp.ltu p6, p7 = r34, r35
; CHECK-NEXT:  mov r8 = r38 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r32, r33
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = 0, r36 ;;
; CHECK-NEXT:  (p6) mov r8 = r37
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp ult i64 %c, %d
  %c3 = icmp ne i64 %e, 0
  %and = and i1 %c1, %c2
  %and2 = and i1 %and, %c3
  %r = select i1 %and2, i64 %x, i64 %y
  ret i64 %r
}

define i64 @sel_inverted(i64 %a, i64 %b, i64 %c, i64 %d, i64 %x, i64 %y) {
; An inner select with its values the other way round tests the inverse.
; CHECK-LABEL: sel_inverted:
; CHECK:       cmp.eq p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r37 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = r34, r35 ;;
; CHECK-NEXT:  (p6) mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp eq i64 %c, %d
  %i = select i1 %c2, i64 %y, i64 %x
  %r = select i1 %c1, i64 %i, i64 %y
  ret i64 %r
}

define i64 @sel_two_plain(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %x,
                          i64 %y) {
; Only one compare can lack a parallel form, so the chain stops at the second,
; whose select gives the value selected.
; CHECK-LABEL: sel_two_plain:
; CHECK:       mov r8 = r38
; CHECK-NEXT:  cmp.ltu p6, p7 = r35, r36 ;;
; CHECK-NEXT:  mov r14 = r8 ;;
; CHECK-NEXT:  (p6) mov r14 = r37
; CHECK-NEXT:  cmp.ltu p6, p7 = r34, r35 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r32, r33 ;;
; CHECK-NEXT:  (p6) mov r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp ult i64 %c, %d
  %c3 = icmp ugt i64 %e, %d
  %and = and i1 %c1, %c2
  %and2 = and i1 %and, %c3
  %r = select i1 %and2, i64 %x, i64 %y
  ret i64 %r
}

define i64 @sel_fp(i64 %a, i64 %b, double %c, double %d, i64 %x, i64 %y) {
; An FP compare goes first, as it has no parallel form.
; CHECK-LABEL: sel_fp:
; CHECK:       fcmp.eq p6, p7 = f8, f9
; CHECK-NEXT:  mov r8 = r37 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = r32, r33 ;;
; CHECK-NEXT:  (p6) mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = fcmp oeq double %c, %d
  %and = and i1 %c1, %c2
  %r = select i1 %and, i64 %x, i64 %y
  ret i64 %r
}

define double @sel_fp_inverted(double %a, double %b, i64 %c, double %x,
                               double %y) {
; The inverse of an FP compare holds for unordered operands.
; CHECK-LABEL: sel_fp_inverted:
; CHECK:       fcmp.lt p7, p6 = f8, f9 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = 0, r34 ;;
; CHECK-NEXT:  (p6) mov f11 = f10 ;;
; CHECK-NEXT:  mov f8 = f11
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %c, 0
  %c2 = fcmp olt double %a, %b
  %i = select i1 %c2, double %y, double %x
  %r = select i1 %c1, double %i, double %y
  ret double %r
}

define i64 @sel_two_fp(double %a, double %b, double %c, i64 %x, i64 %y) {
; The chain stops at a second FP compare.
; CHECK-LABEL: sel_two_fp:
; CHECK:       mov r8 = r36
; CHECK-NEXT:  fcmp.lt p6, p7 = f9, f10 ;;
; CHECK-NEXT:  mov r14 = r8 ;;
; CHECK-NEXT:  (p6) mov r14 = r35
; CHECK-NEXT:  fcmp.lt p6, p7 = f8, f9 ;;
; CHECK-NEXT:  (p6) mov r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = fcmp olt double %a, %b
  %c2 = fcmp olt double %b, %c
  %and = and i1 %c1, %c2
  %r = select i1 %and, i64 %x, i64 %y
  ret i64 %r
}

define i64 @sel_shared(i64 %a, i64 %b, i64 %c, i64 %d, i64 %x, i64 %y,
                       ptr %p) {
; An inner select with another user is selected on its own.
; CHECK-LABEL: sel_shared:
; CHECK:       mov r8 = r37
; CHECK-NEXT:  cmp.eq p6, p7 = r34, r35 ;;
; CHECK-NEXT:  mov r14 = r8 ;;
; CHECK-NEXT:  (p6) mov r14 = r36
; CHECK-NEXT:  cmp.eq p6, p7 = r32, r33 ;;
; CHECK-NEXT:  (p6) mov r8 = r14
; CHECK-NEXT:  st8 [r38] = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c1 = icmp eq i64 %a, %b
  %c2 = icmp eq i64 %c, %d
  %i = select i1 %c2, i64 %x, i64 %y
  store i64 %i, ptr %p
  %r = select i1 %c1, i64 %i, i64 %y
  ret i64 %r
}
