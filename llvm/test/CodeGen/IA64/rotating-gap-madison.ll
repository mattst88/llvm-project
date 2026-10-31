; RUN: llc -mtriple=ia64-unknown-linux-gnu -mcpu=madison < %s \
; RUN:   | FileCheck %s --check-prefix=MADISON
; RUN: llc -mtriple=ia64-unknown-linux-gnu -mcpu=madison \
; RUN:   -ia64-gap-before-rotating=0 < %s | FileCheck %s --check-prefix=NOGAP
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s \
; RUN:   | FileCheck %s --check-prefix=NOGAP

; Madison takes 14 cycles more over a rotating loop of 5 stages whose passes
; are 1 modulo 4 if it is entered right after the writes that set it up, so an
; empty group goes between them. 11 stages do not need one.

; MADISON-LABEL: sparse:
; MADISON:      mov ar.ec = 5
; MADISON:      mov pr.rot = 65536 ;;
; MADISON-NEXT: nop 0 ;;
; MADISON:      br.ctop

; NOGAP-LABEL: sparse:
; NOGAP:      mov pr.rot = 65536 ;;
; NOGAP-NOT:  nop 0
; NOGAP:      br.ctop
define double @sparse(ptr noalias %v, ptr noalias %x, ptr noalias %col, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %loop ]
  %vp = getelementptr inbounds double, ptr %v, i64 %i
  %vv = load double, ptr %vp
  %cp = getelementptr inbounds i32, ptr %col, i64 %i
  %c = load i32, ptr %cp
  %cx = sext i32 %c to i64
  %xp = getelementptr inbounds double, ptr %x, i64 %cx
  %xv = load double, ptr %xp
  %s.next = call double @llvm.fma.f64(double %vv, double %xv, double %s)
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret double %s.next
}

; MADISON-LABEL: chain:
; MADISON:      mov ar.ec = 11
; MADISON-NEXT: mov pr.rot = 65536 ;;
; MADISON-NOT:  nop 0 ;;
; MADISON:      br.ctop
define void @chain(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %x, i64 %i
  %v = load double, ptr %p
  %m = fmul double %v, %a
  %q = getelementptr inbounds double, ptr %y, i64 %i
  store double %m, ptr %q
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

declare double @llvm.fma.f64(double, double, double)
