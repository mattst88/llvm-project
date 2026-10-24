; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-hoist-compare-depth=1 < %s \
; RUN:   | FileCheck %s --check-prefix=DEPTH1
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-rename-branch-predicates=false < %s \
; RUN:   | FileCheck %s --check-prefix=SAME

; Each branch of a chain that falls through from one test to the next writes
; its own pair of predicates, so that the compares do not write the same
; predicates as the one before and need no stop between them. All three
; compares go before the first branch, which the second and third blocks
; fall through to holding only their branches.

declare void @f(i64)

define void @chain(ptr %p) {
; CHECK-LABEL: chain:
; CHECK:       ld1 [[C:r[0-9]+]] = [r32]
; CHECK:       cmp.eq p6, p7 = 0, [[C]]
; CHECK-NEXT:  cmp.eq p8, p9 = 44, [[C]]
; CHECK-NEXT:  cmp.eq p10, p11 = 46, [[C]]
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB0_{{[0-9]+}}{{$}}
; CHECK-NEXT:  // %bb.1:
; CHECK-NEXT:  (p8) br.cond.dpnt.few .LBB0_{{[0-9]+}}{{$}}
; CHECK-NEXT:  // %bb.2:
; CHECK-NEXT:  (p10) br.cond.dpnt.few .LBB0_{{[0-9]+}}{{$}}
;
; DEPTH1-LABEL: chain:
; DEPTH1:       cmp.eq p6, p7 = 0, [[C:r[0-9]+]]
; DEPTH1-NEXT:  cmp.eq p8, p9 = 44, [[C]]
; DEPTH1-NEXT:  (p6) br.cond.dpnt.few .LBB0_{{[0-9]+}}{{$}}
; DEPTH1-NEXT:  // %bb.1:
; DEPTH1-NEXT:  cmp.eq p10, p11 = 46, [[C]]
; DEPTH1-NEXT:  (p8) br.cond.dpnt.few .LBB0_{{[0-9]+}}{{$}}
;
; SAME-LABEL:  chain:
; SAME:        cmp.eq p6, p7 = 0, [[C:r[0-9]+]]
; SAME-NEXT:   (p6) br.cond.dpnt.few .LBB0_{{[0-9]+}} ;;
; SAME-NEXT:   // %bb.1:
; SAME-NEXT:   cmp.eq p6, p7 = 44, [[C]]
; SAME-NEXT:   (p6) br.cond.dpnt.few .LBB0_{{[0-9]+}} ;;
; SAME-NEXT:   // %bb.2:
; SAME-NEXT:   cmp.eq p6, p7 = 46, [[C]]
entry:
  %c = load i8, ptr %p
  %z = icmp eq i8 %c, 0
  br i1 %z, label %a, label %t1, !prof !0
t1:
  %comma = icmp eq i8 %c, 44
  br i1 %comma, label %b, label %t2, !prof !0
t2:
  %dot = icmp eq i8 %c, 46
  br i1 %dot, label %c3, label %d, !prof !0
a:
  call void @f(i64 1)
  ret void
b:
  call void @f(i64 2)
  ret void
c3:
  call void @f(i64 3)
  ret void
d:
  call void @f(i64 4)
  ret void
}

!0 = !{!"branch_weights", i32 1, i32 100}
