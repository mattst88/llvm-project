; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; A constant that a loop passes to a call is formed again in the loop rather
; than kept in a stacked register across the call: an addl from r0 costs no
; more than the copy it replaces.

declare void @g(i64, i64)

; CHECK-LABEL: loop:
; CHECK:       alloc {{r[0-9]+}} = ar.pfs, 2, 4, 2, 0
; CHECK:       .LBB0_1:
; CHECK-DAG:   mov r{{[0-9]+}} = 1000
; CHECK-DAG:   mov r{{[0-9]+}} = 2000
; CHECK:       br.call.sptk.many b0 = g
define void @loop(ptr %p, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  call void @g(i64 1000, i64 2000)
  %v = load i32, ptr %p
  %i.next = add i32 %i, %v
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
