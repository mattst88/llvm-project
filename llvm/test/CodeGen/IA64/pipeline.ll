; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-pipeliner=false < %s | FileCheck %s --check-prefix=OFF

; The load of each element issues in the iteration before the add of it, so
; the add need not wait for the load. The prologue loads the first element
; and starts the kernel with a br.cloop, which leaves for the epilogue
; instead when that was the only element; the epilogue adds the last one.
define i64 @sum(ptr %a, i64 %n) {
; CHECK-LABEL: sum:
; CHECK:       mov ar.lc = r{{[0-9]+}}
; CHECK:       ld8 [[V:r[0-9]+]] = [r32], 8 ;;
; CHECK-NEXT:  br.cloop.sptk.few .LBB0_[[KERNEL:[0-9]+]]
; CHECK-NEXT:  br.cond.sptk.few .LBB0_[[EPILOGUE:[0-9]+]] ;;
; CHECK-NEXT:  .LBB0_[[KERNEL]]:
; CHECK:       add [[S:r[0-9]+]] = [[S]], [[V]]
; CHECK-NEXT:  ld8 [[V]] = [r32], 8
; CHECK-NEXT:  br.cloop.sptk.few .LBB0_[[KERNEL]] ;;
; CHECK-NEXT:  .LBB0_[[EPILOGUE]]:
; CHECK-NEXT:  add r8 = [[S]], [[V]]
;
; OFF-LABEL:   sum:
; OFF:         mov ar.lc =
; OFF-NEXT:    .LBB0_[[LOOP:[0-9]+]]:
; OFF:         ld8
; OFF:         br.cloop.sptk.few .LBB0_[[LOOP]] ;;
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %s.next = add i64 %s, %v
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  %r = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  ret i64 %r
}

; Each store may alias the next iteration's loads, so no schedule overlaps
; two iterations and the loop is left as it is.
define void @axpy(ptr %y, ptr %x, double %a, i64 %n) {
; CHECK-LABEL: axpy:
; CHECK:       .LBB1_[[LOOP:[0-9]+]]:
; CHECK:       ldfd
; CHECK:       fma.d
; CHECK:       stfd
; CHECK-NEXT:  br.cloop.sptk.few .LBB1_[[LOOP]] ;;
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr double, ptr %x, i64 %i
  %py = getelementptr double, ptr %y, i64 %i
  %vx = load double, ptr %px
  %vy = load double, ptr %py
  %m = fmul contract double %a, %vx
  %r = fadd contract double %m, %vy
  store double %r, ptr %py
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
