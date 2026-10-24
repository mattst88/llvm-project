; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-rotating-static-accumulators=false < %s \
; RUN:   | FileCheck %s --check-prefix=OFF

; The sum of a rotating loop, which only the fma that adds to it reads, stays
; in one register, the one the function returns it in: the fma, predicated
; off while the loop fills and drains, leaves it as it was, so nothing copies
; it out of a rotating register after the loop, waiting for the last fma.
; Without that, the sum rotates through f40 and f41 and is copied out.
define double @dot(ptr %x, ptr %v, ptr %c, i64 %n) {
; CHECK-LABEL: dot:
; CHECK:       mov f8 = f0
; CHECK:       mov pr.rot = 65536 ;;
; CHECK:       (p20) fma.d f8 = f{{[0-9]+}}, f{{[0-9]+}}, f8
; CHECK:       br.ctop.sptk.few
; CHECK-NEXT:  // %bb
; CHECK-NEXT:  clrrrb ;;
;
; OFF-LABEL:   dot:
; OFF:         (p20) fma.d f40 = f{{[0-9]+}}, f{{[0-9]+}}, f41
; OFF:         br.ctop.sptk.few
; OFF-NEXT:    // %bb
; OFF-NEXT:    mov f8 = f41
; OFF-NEXT:    clrrrb ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %loop ]
  %cp = getelementptr i32, ptr %c, i64 %i
  %ci = load i32, ptr %cp
  %ce = sext i32 %ci to i64
  %xp = getelementptr double, ptr %x, i64 %ce
  %xv = load double, ptr %xp
  %vp = getelementptr double, ptr %v, i64 %i
  %vv = load double, ptr %vp
  %m = fmul contract double %xv, %vv
  %s.next = fadd contract double %s, %m
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret double %s.next
}

; A value a later pass reads still rotates, and is copied out after the loop.
define double @last(ptr noalias %x, ptr noalias %c, ptr noalias %o, i64 %n) {
; CHECK-LABEL: last:
; CHECK:       br.ctop.sptk.few
; CHECK-NEXT:  // %bb
; CHECK-NEXT:  mov f8 = f{{3[2-9]|[4-9][0-9]}}
; CHECK-NEXT:  clrrrb ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %cp = getelementptr i32, ptr %c, i64 %i
  %ci = load i32, ptr %cp
  %ce = sext i32 %ci to i64
  %xp = getelementptr double, ptr %x, i64 %ce
  %xv = load double, ptr %xp
  %op = getelementptr double, ptr %o, i64 %i
  store double %xv, ptr %op
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret double %xv
}
