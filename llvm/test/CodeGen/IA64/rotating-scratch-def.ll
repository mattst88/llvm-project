; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; An FP compare whose result a loop keeps in a general register is a SETCCf,
; which writes its result through p6 and p7. Those are dead after it, so they
; do not stop the loop from rotating.
define void @negative(ptr noalias %a, ptr noalias %o, ptr noalias %e, i64 %n) {
; CHECK-LABEL: negative:
; CHECK:       mov pr.rot = 65536 ;;
; CHECK:       .LBB0_[[LOOP:[0-9]+]]:
; CHECK:       fcmp.lt p6, p7 = f{{[0-9]+}}, f0
; CHECK:       (p6) mov r{{[0-9]+}} = 1
; CHECK-NEXT:  br.ctop.sptk.few .LBB0_[[LOOP]] ;;
; CHECK-NEXT:  // %bb
; CHECK-NEXT:  clrrrb ;;
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %neg = phi i32 [ 0, %entry ], [ %neg1, %loop ]
  %pa = getelementptr double, ptr %a, i64 %i
  %x = load double, ptr %pa
  %m = fmul double %x, %x
  %d = fadd double %m, %x
  %lt = fcmp olt double %x, 0.0
  %z = zext i1 %lt to i32
  %neg1 = or i32 %neg, %z
  %po = getelementptr double, ptr %o, i64 %i
  store double %d, ptr %po
  %i1 = add i64 %i, 1
  %c = icmp ne i64 %i1, %n
  br i1 %c, label %loop, label %exit

exit:
  store i32 %neg1, ptr %e
  ret void
}
