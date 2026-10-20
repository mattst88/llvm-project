; RUN: opt -S -passes=loop-reduce < %s | FileCheck %s

; Loop strength reduction counts instructions before registers. An access
; has no immediate offset on IA-64, so reaching y[i + 1] from the pointer to
; y[i] takes an add each iteration; with a pointer of its own, the add that
; steps it is the only one.
; CHECK-LABEL: @daxpy2(
; CHECK:       loop:
; CHECK-NEXT:    phi i64
; CHECK-NEXT:    [[Y0:%.*]] = phi ptr [ {{%.*}}, %loop ], [ %y, %entry ]
; CHECK-NEXT:    [[X0:%.*]] = phi ptr [ {{%.*}}, %loop ], [ %x, %entry ]
; CHECK-NEXT:    [[Y1:%.*]] = phi ptr
; CHECK-NEXT:    [[X1:%.*]] = phi ptr
; CHECK-NEXT:    load double, ptr [[Y0]]
; CHECK-NEXT:    load double, ptr [[X0]]
; CHECK-NEXT:    fmul
; CHECK-NEXT:    fadd
; CHECK-NEXT:    store double {{%.*}}, ptr [[Y0]]
; CHECK-NEXT:    load double, ptr [[Y1]]
; CHECK-NEXT:    load double, ptr [[X1]]
; CHECK-NEXT:    fmul
; CHECK-NEXT:    fadd
; CHECK-NEXT:    store double {{%.*}}, ptr [[Y1]]

target datalayout = "e-m:e-p:64:64-i64:64-i128:128-f80:128-n64-S128"
target triple = "ia64-unknown-linux-gnu"

define void @daxpy2(ptr %y, ptr %x, double %a, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %k = phi i64 [ 0, %entry ], [ %k.next, %loop ]
  %py0 = getelementptr inbounds nuw [8 x i8], ptr %y, i64 %i
  %y0 = load double, ptr %py0
  %px0 = getelementptr inbounds nuw [8 x i8], ptr %x, i64 %i
  %x0 = load double, ptr %px0
  %m0 = fmul double %a, %x0
  %s0 = fadd double %y0, %m0
  store double %s0, ptr %py0
  %i1 = or disjoint i64 %i, 1
  %py1 = getelementptr inbounds nuw [8 x i8], ptr %y, i64 %i1
  %y1 = load double, ptr %py1
  %px1 = getelementptr inbounds nuw [8 x i8], ptr %x, i64 %i1
  %x1 = load double, ptr %px1
  %m1 = fmul double %a, %x1
  %s1 = fadd double %y1, %m1
  store double %s1, ptr %py1
  %i.next = add nuw nsw i64 %i, 2
  %k.next = add i64 %k, 2
  %c = icmp eq i64 %k.next, %n
  br i1 %c, label %exit, label %loop
exit:
  ret void
}
