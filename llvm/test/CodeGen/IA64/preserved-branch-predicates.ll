; RUN: opt -mtriple=ia64 -passes=ia64-speculate-chases -S < %s \
; RUN:   | llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs | FileCheck %s

; A chain of branches, each alone in a block only the one before reaches,
; has its compares all above the first branch, so that the branches issue
; together. Five fit in the scratch pairs p6-p15. Past that, in a speculated
; list search, the prologue saves the predicates and the compares use p16
; and up; a chain without NaT tests stays in the scratch pairs.

; CHECK-LABEL: five:
; CHECK-NOT:     {{= pr|pr =}}
; CHECK:         cmp.eq p6, p7 = 3, r32
; CHECK:         cmp.eq p8, p9 = 4, r33
; CHECK:         cmp.eq p10, p11 = 5, r34
; CHECK:         cmp.eq p12, p13 = 6, r35
; CHECK:         (p6) br.cond.dpnt.few
; CHECK:         (p8) br.cond.dpnt.few
; CHECK:         (p10) br.cond.dpnt.few
; CHECK:         (p12) br.cond.dpnt.few
; CHECK-NOT:     {{= pr|pr =}}
; CHECK:         .endp five
define i64 @five(i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4) {
  %c0 = icmp eq i64 %a0, 3
  br i1 %c0, label %r0, label %t1, !prof !0
t1:
  %c1 = icmp eq i64 %a1, 4
  br i1 %c1, label %r1, label %t2, !prof !0
t2:
  %c2 = icmp eq i64 %a2, 5
  br i1 %c2, label %r2, label %t3, !prof !0
t3:
  %c3 = icmp eq i64 %a3, 6
  br i1 %c3, label %r3, label %t4, !prof !0
t4:
  %c4 = icmp eq i64 %a4, 7
  br i1 %c4, label %r4, label %none, !prof !0
r0:
  ret i64 0
r1:
  ret i64 1
r2:
  ret i64 2
r3:
  ret i64 3
r4:
  ret i64 4
none:
  ret i64 -1
}

; CHECK-LABEL: seven:
; CHECK-NOT:     {{= pr|pr =}}
; CHECK:         cmp.eq p14, p15 = 7, r36
; CHECK:         (p6) br.cond.dpnt.few
; CHECK-NOT:     {{p1[6-9]|p2[0-9]|p3[01]|= pr|pr =}}
; CHECK:         .endp seven
define i64 @seven(i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6) {
  %c0 = icmp eq i64 %a0, 3
  br i1 %c0, label %r0, label %t1, !prof !0
t1:
  %c1 = icmp eq i64 %a1, 4
  br i1 %c1, label %r1, label %t2, !prof !0
t2:
  %c2 = icmp eq i64 %a2, 5
  br i1 %c2, label %r2, label %t3, !prof !0
t3:
  %c3 = icmp eq i64 %a3, 6
  br i1 %c3, label %r3, label %t4, !prof !0
t4:
  %c4 = icmp eq i64 %a4, 7
  br i1 %c4, label %r4, label %t5, !prof !0
t5:
  %c5 = icmp eq i64 %a5, 8
  br i1 %c5, label %r5, label %t6, !prof !0
t6:
  %c6 = icmp eq i64 %a6, 9
  br i1 %c6, label %r6, label %none, !prof !0
r0:
  ret i64 0
r1:
  ret i64 1
r2:
  ret i64 2
r3:
  ret i64 3
r4:
  ret i64 4
r5:
  ret i64 5
r6:
  ret i64 6
none:
  ret i64 -1
}

; The speculated pass of the search goes on by one branch after and-type
; compares, in one instruction group, which also stand for the NaT tests of
; what they read. The chain of branches finding where the pass leaves comes
; after it.

; CHECK-LABEL: find:
; CHECK:         mov [[PR:r[0-9]+]] = pr
; CHECK:       .LBB2_2:
; CHECK-NOT:     tnat
; CHECK:         cmp.eq p6, p7 = r0, r0
; CHECK-NOT:     tnat
; CHECK:         ;;
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    cmp.ne.and p6, p0 = {{[^;]+$}}
; CHECK-NEXT:    (p6) br.cond.dptk.few .LBB2_2
; CHECK-NEXT:  // %bb.3:
; CHECK-NEXT:    cmp.eq p9, p8 = r{{[0-9]+}}, r14
; CHECK:       .LBB2_6:
; CHECK:         tnat.z p7, p6 =
; CHECK:         cmp.eq p16, p17 = 0,
; CHECK:         (p6) br.cond.dpnt.few
; CHECK:         (p16) br.cond.dpnt.few
; CHECK:         mov pr = [[PR]], 65598
; CHECK-NEXT:    br.ret.sptk.many b0
define ptr @find(ptr %list, i16 %key) {
entry:
  %empty = icmp eq ptr %list, null
  br i1 %empty, label %return, label %loop

loop:
  %l = phi ptr [ %next, %body ], [ %list, %entry ]
  %infop = getelementptr inbounds nuw i8, ptr %l, i64 8
  %info = load ptr, ptr %infop, align 8
  %idxp = getelementptr inbounds nuw i8, ptr %info, i64 2
  %idx = load i16, ptr %idxp, align 2
  %found = icmp eq i16 %idx, %key
  br i1 %found, label %return, label %body

body:
  %next = load ptr, ptr %l, align 8
  %end = icmp eq ptr %next, null
  br i1 %end, label %return, label %loop

return:
  %r = phi ptr [ null, %entry ], [ %l, %loop ], [ null, %body ]
  ret ptr %r
}

!0 = !{!"branch_weights", i32 1, i32 100}
