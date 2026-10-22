; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -ia64-single-adds-in-double=0 \
; RUN:   < %s | FileCheck %s --check-prefix=OFF

; A single-precision add in a loop is made in double precision and rounded
; to single, which never stalls on the 9340.
; CHECK-LABEL: add:
; CHECK: fma.d [[T:f[0-9]+]] = f{{[0-9]+}}, f1, f{{[0-9]+}}
; CHECK: fma.s f{{[0-9]+}} = [[T]], f1, f0
; CHECK-NOT: fma.s {{.*}}, f1, f{{[1-9]}}
; OFF-LABEL: add:
; OFF-NOT: fma.d
; OFF: fma.s f{{[0-9]+}} = f{{[0-9]+}}, f1, f{{[0-9]+}}
define void @add(ptr %c, ptr %a, ptr %b, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %pa = getelementptr float, ptr %a, i64 %i
  %pb = getelementptr float, ptr %b, i64 %i
  %x = load float, ptr %pa
  %y = load float, ptr %pb
  %s = fadd float %x, %y
  %pc = getelementptr float, ptr %c, i64 %i
  store float %s, ptr %pc
  %i1 = add i64 %i, 1
  %done = icmp eq i64 %i1, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; So is a subtraction.
; CHECK-LABEL: sub:
; CHECK: fms.d [[T:f[0-9]+]] = f{{[0-9]+}}, f1, f{{[0-9]+}}
; CHECK: fma.s f{{[0-9]+}} = [[T]], f1, f0
define void @sub(ptr %c, ptr %a, ptr %b, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %pa = getelementptr float, ptr %a, i64 %i
  %pb = getelementptr float, ptr %b, i64 %i
  %x = load float, ptr %pa
  %y = load float, ptr %pb
  %s = fsub float %x, %y
  %pc = getelementptr float, ptr %c, i64 %i
  store float %s, ptr %pc
  %i1 = add i64 %i, 1
  %done = icmp eq i64 %i1, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; A running sum is a recurrence, where the extra latency would cost more
; than the stall saves.
; CHECK-LABEL: sum:
; CHECK-NOT: fma.d
; CHECK: fma.s f{{[0-9]+}} = f{{[0-9]+}}, f1, f{{[0-9]+}}
; CHECK-NOT: fma.d
; CHECK: br.ret
define float @sum(ptr %a, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %s = phi float [ 0.0, %entry ], [ %s1, %loop ]
  %pa = getelementptr float, ptr %a, i64 %i
  %x = load float, ptr %pa
  %s1 = fadd float %s, %x
  %i1 = add i64 %i, 1
  %done = icmp eq i64 %i1, %n
  br i1 %done, label %exit, label %loop

exit:
  ret float %s1
}

; So is a recurrence through two adds.
; CHECK-LABEL: sum2:
; CHECK-NOT: fma.d
; CHECK: br.ret
define float @sum2(ptr %a, ptr %b, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %s = phi float [ 0.0, %entry ], [ %s2, %loop ]
  %pa = getelementptr float, ptr %a, i64 %i
  %pb = getelementptr float, ptr %b, i64 %i
  %x = load float, ptr %pa
  %y = load float, ptr %pb
  %s1 = fadd float %s, %x
  %s2 = fadd float %s1, %y
  %i1 = add i64 %i, 1
  %done = icmp eq i64 %i1, %n
  br i1 %done, label %exit, label %loop

exit:
  ret float %s2
}

; Outside a loop an add is left alone.
; CHECK-LABEL: straight:
; CHECK-NOT: fma.d
; CHECK: fma.s f8 = f8, f1, f9
define float @straight(float %a, float %b) {
  %s = fadd float %a, %b
  ret float %s
}

; Double and extended adds are left alone.
; CHECK-LABEL: dadd:
; CHECK: fma.d f{{[0-9]+}} = f{{[0-9]+}}, f1, f{{[0-9]+}}
; CHECK-NOT: fma.s
; CHECK: br.ret
define void @dadd(ptr %c, ptr %a, ptr %b, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %pa = getelementptr double, ptr %a, i64 %i
  %pb = getelementptr double, ptr %b, i64 %i
  %x = load double, ptr %pa
  %y = load double, ptr %pb
  %s = fadd double %x, %y
  %pc = getelementptr double, ptr %c, i64 %i
  store double %s, ptr %pc
  %i1 = add i64 %i, 1
  %done = icmp eq i64 %i1, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
