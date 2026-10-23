; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-pipeline-load-banks=0 < %s | FileCheck %s --check-prefix=OFF

; for (j = 1; j < n; j++) ex[j] -= 0.5 * (hz[j] - hz[j - 1]);
; with hz[j - 1] taken from the iteration before. ex and hz, as large as
; each other, put ex[j] and hz[j] in the same L2 bank, so the pipeliner
; issues their loads in different cycles.

; CHECK-LABEL: stencil:
; CHECK:       .LBB0_1:
; CHECK:         ldfd {{f[0-9]+}} = [{{r[0-9]+}}], 8
; CHECK-NOT:     ldfd
; CHECK:         ;;
; CHECK:         ldfd {{f[0-9]+}} = [{{r[0-9]+}}], 8
; CHECK:         br.ctop

; OFF-LABEL: stencil:
; OFF:         .LBB0_1:
; OFF:           ;;
; OFF-NEXT:      ldfd {{f[0-9]+}} = [{{r[0-9]+}}], 8
; OFF-NOT:       ;;
; OFF:           ldfd {{f[0-9]+}} = [{{r[0-9]+}}], 8
; OFF-NOT:       ;;
; OFF:           br.ctop
define void @stencil(ptr noalias %ex, ptr noalias %hz, i64 %n) {
entry:
  %first = load double, ptr %hz
  br label %loop
loop:
  %j = phi i64 [ 0, %entry ], [ %j.next, %loop ]
  %prev = phi double [ %first, %entry ], [ %c, %loop ]
  %j.next = add nuw nsw i64 %j, 1
  %pe = getelementptr inbounds double, ptr %ex, i64 %j.next
  %e = load double, ptr %pe
  %pc = getelementptr inbounds double, ptr %hz, i64 %j.next
  %c = load double, ptr %pc
  %d = fsub double %c, %prev
  %m = fmul double %d, 5.0e-01
  %r = fsub double %e, %m
  store double %r, ptr %pe
  %done = icmp eq i64 %j.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
