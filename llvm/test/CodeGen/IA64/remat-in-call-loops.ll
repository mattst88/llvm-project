; RUN: llc -mtriple=ia64 -O2 -ia64-remat-min-live=0 < %s | FileCheck %s
; RUN: llc -mtriple=ia64 -O2 -ia64-remat-min-live=0 \
; RUN:   -ia64-remat-call-ratio=0 < %s | FileCheck %s --check-prefix=HOIST
; RUN: llc -mtriple=ia64 -O2 -ia64-remat-min-live=9 < %s \
; RUN:   | FileCheck %s --check-prefix=HOIST

; Addresses formed before a loop that calls are formed again in the loop,
; rather than kept in stacked registers across the call, in a function that
; keeps at least -ia64-remat-min-live registers live across a call. @fields
; keeps 8.

declare void @g(ptr, ptr, ptr, i64)

; CHECK-LABEL: fields:
; CHECK:       alloc {{r[0-9]+}} = ar.pfs, 2, 4, 4, 0
; CHECK:       .LBB0_1:
; CHECK-DAG:   adds r{{[0-9]+}} = 8, r32
; CHECK-DAG:   adds r{{[0-9]+}} = 16, r32
; CHECK-DAG:   adds r{{[0-9]+}} = 24, r32
; CHECK-DAG:   mov r{{[0-9]+}} = 1000
; CHECK:       br.call.sptk.many b0 = g
; HOIST-LABEL: fields:
; HOIST:       alloc {{r[0-9]+}} = ar.pfs, 2, 8, 4, 0
define void @fields(ptr %p, i32 %n) {
entry:
  %a = getelementptr inbounds i8, ptr %p, i64 8
  %b = getelementptr inbounds i8, ptr %p, i64 16
  %c = getelementptr inbounds i8, ptr %p, i64 24
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  call void @g(ptr %a, ptr %b, ptr %c, i64 1000)
  %v = load i32, ptr %p
  %i.next = add i32 %i, %v
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; A use in a hot block of a loop whose call is rare keeps the hoisted value.

; CHECK-LABEL: cold_call:
; CHECK:       adds [[A:r[0-9]+]] = 8, r32
; CHECK:       .LBB1_1:
; CHECK-NOT:   adds {{.*}} = 8, r32
; CHECK:       .LBB1_{{[0-9]+}}:
define void @cold_call(ptr %p, i32 %n) {
entry:
  %a = getelementptr inbounds i8, ptr %p, i64 8
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %v = load i32, ptr %a
  %rare = icmp eq i32 %v, 12345
  br i1 %rare, label %call, label %latch, !prof !0

call:
  call void @g(ptr %p, ptr %p, ptr %p, i64 0)
  br label %latch

latch:
  %i.next = add i32 %i, %v
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

!0 = !{!"branch_weights", i32 1, i32 1000}

; A leaf this module defines cannot overflow the register stack, so a loop
; that calls only it keeps the hoisted values.

define void @leaf(ptr %a, ptr %b, ptr %c, i64 %n) noinline {
  store i64 %n, ptr %a
  store i64 %n, ptr %b
  store i64 %n, ptr %c
  ret void
}

; CHECK-LABEL: leaf_call:
; CHECK:       alloc {{r[0-9]+}} = ar.pfs, 2, 8, 4, 0
; CHECK:       .LBB3_1:
; CHECK-NOT:   adds {{.*}}, r32
; CHECK:       br.call.sptk.many b0 = leaf
define void @leaf_call(ptr %p, i32 %n) {
entry:
  %a = getelementptr inbounds i8, ptr %p, i64 8
  %b = getelementptr inbounds i8, ptr %p, i64 16
  %c = getelementptr inbounds i8, ptr %p, i64 24
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  call void @leaf(ptr %a, ptr %b, ptr %c, i64 1000)
  %v = load i32, ptr %p
  %i.next = add i32 %i, %v
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
