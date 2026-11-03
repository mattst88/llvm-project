; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A branch on an AND of integer compares, kept whole by !unpredictable, is
; one compare and and-type compares clearing its predicate, all in one
; instruction group, then the branch. Normal and and-type compares clear
; their predicates if an operand is NaT, so a test that a value is not NaT
; goes where a compare reads it or a value it makes NaT. The branch is
; never inverted, which would take it on a NaT.

; All have and-type forms, so the ordinary compare is of a condition on
; arguments, ahead of the one on the load. %b is read by "%b + 1 == 5",
; which is "%b == 4", and %c through a speculative load's address, so only
; the NaT test of %d is left.
; CHECK-LABEL: all:
; CHECK-NOT:     tnat
; CHECK:         tnat.z p6, p7 = r35
; CHECK-NEXT:    ld8.s
; CHECK-NEXT:    cmp.eq.and p6, p0 = 4, r33
; CHECK-NEXT:    cmp.ne.and p6, p0 = 0, r32
; CHECK-NEXT:    cmp.ne.and p6, p0 = r{{[0-9]+}}, r33
; CHECK-NEXT:    (p6) br.cond.dptk.few
; CHECK-NOT:     tnat
; CHECK:         .endp all
define i64 @all(i64 %a, i64 %b, ptr %c, i64 %d) {
entry:
  %b1 = add i64 %b, 1
  %l = call i64 @llvm.ia64.ld8.s(ptr %c)
  %c0 = icmp ne i64 %a, 0
  %c1 = icmp eq i64 %b1, 5
  %c2 = icmp ne i64 %l, %b
  %tb = call i64 @llvm.ia64.tnat(i64 %b)
  %n1 = icmp eq i64 %tb, 0
  %ci = ptrtoint ptr %c to i64
  %tc = call i64 @llvm.ia64.tnat(i64 %ci)
  %n2 = icmp eq i64 %tc, 0
  %td = call i64 @llvm.ia64.tnat(i64 %d)
  %n3 = icmp eq i64 %td, 0
  %x0 = and i1 %c0, %c1
  %x1 = and i1 %x0, %c2
  %x2 = and i1 %x1, %n1
  %x3 = and i1 %x2, %n2
  %x4 = and i1 %x3, %n3
  br i1 %x4, label %yes, label %no, !prof !0, !unpredictable !1

no:
  ret i64 0

yes:
  %r = call i64 @f()
  ret i64 %r
}

; Every condition is on the result of a load: "cmp.eq p6, p7 = r0, r0"
; starts them, since an ordinary compare of either would wait for its load.
; CHECK-LABEL: loads:
; CHECK:         cmp.eq p6, p7 = r0, r0
; CHECK:         cmp.ne.and p6, p0 = 0, r{{[0-9]+}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = 0, r{{[0-9]+}}
; CHECK-NEXT:    (p6) br.cond.dptk.few
define i64 @loads(ptr %p, ptr %q) {
entry:
  %a = load i64, ptr %p
  %b = load i64, ptr %q
  %c0 = icmp ne i64 %a, 0
  %c1 = icmp ne i64 %b, 0
  %x = and i1 %c0, %c1
  br i1 %x, label %yes, label %no, !prof !0, !unpredictable !1

no:
  ret i64 0

yes:
  %r = call i64 @f()
  ret i64 %r
}

; An unsigned compare has no and-type form, so it goes first, a group ahead.
; CHECK-LABEL: plain:
; CHECK:         cmp.ltu p6, p7 = r32, r33 ;;
; CHECK-NEXT:    cmp.ne.and p6, p0 = 0, r34
; CHECK-NEXT:    (p6) br.cond.dptk.few
define i64 @plain(i64 %a, i64 %b, i64 %c) {
entry:
  %c0 = icmp ult i64 %a, %b
  %c1 = icmp ne i64 %c, 0
  %x = and i1 %c0, %c1
  br i1 %x, label %yes, label %no, !prof !0, !unpredictable !1

no:
  ret i64 0

yes:
  %r = call i64 @f()
  ret i64 %r
}

; With two such compares, the second is under the first's predicate.
; CHECK-LABEL: two_plain:
; CHECK:         cmp.ltu p6, p7 = r32, r33 ;;
; CHECK-NEXT:    (p6) cmp.ltu p6, p7 = r34, r35
; CHECK-NEXT:    (p6) br.cond.dptk.few
define i64 @two_plain(i64 %a, i64 %b, i64 %c, i64 %d) {
entry:
  %c0 = icmp ult i64 %a, %b
  %c1 = icmp ult i64 %c, %d
  %x = and i1 %c0, %c1
  br i1 %x, label %yes, label %no, !prof !0, !unpredictable !1

no:
  ret i64 0

yes:
  %r = call i64 @f()
  ret i64 %r
}

declare i64 @f()
declare i64 @llvm.ia64.ld8.s(ptr)
declare i64 @llvm.ia64.tnat(i64)

!0 = !{!"branch_weights", i32 100, i32 1}
!1 = !{}
