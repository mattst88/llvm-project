; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning | FileCheck %s

; A loop that stores to d and reads eight other arrays needs a check of d
; against each of them, which is within the limit.
; CHECK-LABEL: @sum8(
; CHECK:       for.body.lver.check:
; CHECK:       for.body.lver.orig:
define void @sum8(ptr %d, ptr %a0, ptr %a1, ptr %a2, ptr %a3, ptr %a4, ptr %a5, ptr %a6, ptr %a7, i64 %n) {
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %p0 = getelementptr inbounds double, ptr %a0, i64 %i
  %v0 = load double, ptr %p0, align 8
  %p1 = getelementptr inbounds double, ptr %a1, i64 %i
  %v1 = load double, ptr %p1, align 8
  %p2 = getelementptr inbounds double, ptr %a2, i64 %i
  %v2 = load double, ptr %p2, align 8
  %p3 = getelementptr inbounds double, ptr %a3, i64 %i
  %v3 = load double, ptr %p3, align 8
  %p4 = getelementptr inbounds double, ptr %a4, i64 %i
  %v4 = load double, ptr %p4, align 8
  %p5 = getelementptr inbounds double, ptr %a5, i64 %i
  %v5 = load double, ptr %p5, align 8
  %p6 = getelementptr inbounds double, ptr %a6, i64 %i
  %v6 = load double, ptr %p6, align 8
  %p7 = getelementptr inbounds double, ptr %a7, i64 %i
  %v7 = load double, ptr %p7, align 8
  %s1 = fadd double %v0, %v1
  %s2 = fadd double %s1, %v2
  %s3 = fadd double %s2, %v3
  %s4 = fadd double %s3, %v4
  %s5 = fadd double %s4, %v5
  %s6 = fadd double %s5, %v6
  %s7 = fadd double %s6, %v7
  %pd = getelementptr inbounds double, ptr %d, i64 %i
  store double %s7, ptr %pd, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; Nine checks are too many.
; CHECK-LABEL: @sum9(
; CHECK-NOT:   lver
; CHECK:       ret void
define void @sum9(ptr %d, ptr %a0, ptr %a1, ptr %a2, ptr %a3, ptr %a4, ptr %a5, ptr %a6, ptr %a7, ptr %a8, i64 %n) {
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %p0 = getelementptr inbounds double, ptr %a0, i64 %i
  %v0 = load double, ptr %p0, align 8
  %p1 = getelementptr inbounds double, ptr %a1, i64 %i
  %v1 = load double, ptr %p1, align 8
  %p2 = getelementptr inbounds double, ptr %a2, i64 %i
  %v2 = load double, ptr %p2, align 8
  %p3 = getelementptr inbounds double, ptr %a3, i64 %i
  %v3 = load double, ptr %p3, align 8
  %p4 = getelementptr inbounds double, ptr %a4, i64 %i
  %v4 = load double, ptr %p4, align 8
  %p5 = getelementptr inbounds double, ptr %a5, i64 %i
  %v5 = load double, ptr %p5, align 8
  %p6 = getelementptr inbounds double, ptr %a6, i64 %i
  %v6 = load double, ptr %p6, align 8
  %p7 = getelementptr inbounds double, ptr %a7, i64 %i
  %v7 = load double, ptr %p7, align 8
  %p8 = getelementptr inbounds double, ptr %a8, i64 %i
  %v8 = load double, ptr %p8, align 8
  %s1 = fadd double %v0, %v1
  %s2 = fadd double %s1, %v2
  %s3 = fadd double %s2, %v3
  %s4 = fadd double %s3, %v4
  %s5 = fadd double %s4, %v5
  %s6 = fadd double %s5, %v6
  %s7 = fadd double %s6, %v7
  %s8 = fadd double %s7, %v8
  %pd = getelementptr inbounds double, ptr %d, i64 %i
  store double %s8, ptr %pd, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}
