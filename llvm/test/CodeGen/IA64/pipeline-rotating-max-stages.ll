; RUN: llc -mtriple=ia64-unknown-linux-gnu -pass-remarks-analysis=pipeliner \
; RUN:   -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=SIXTEEN
; RUN: llc -mtriple=ia64-unknown-linux-gnu -pass-remarks-analysis=pipeliner \
; RUN:   -mcpu=madison -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SIXTEEN
; RUN: llc -mtriple=ia64-unknown-linux-gnu -pass-remarks-analysis=pipeliner \
; RUN:   -ia64-rotating-max-stages=10 -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TEN

; At II 2 this loop has 16 stages. The default limit on a rotating loop, on
; every CPU, lets the pipeliner keep II 2; a limit of 10 makes it settle for
; II 3.

; SIXTEEN: Schedule found with Initiation Interval: 2
; TEN: Schedule found with Initiation Interval: 3
define void @chain(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %p = getelementptr inbounds double, ptr %x, i64 %i
  %v = load double, ptr %p
  %c1 = fptosi double %v to i64
  %a1 = add i64 %c1, 1
  %f1 = sitofp i64 %a1 to double
  %q = getelementptr inbounds double, ptr %y, i64 %i
  store double %f1, ptr %q
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
