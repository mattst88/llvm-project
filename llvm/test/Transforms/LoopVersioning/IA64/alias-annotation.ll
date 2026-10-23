; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning | FileCheck %s

@a = internal global [40000 x float] zeroinitializer, align 4

; Two rows of one array, as linpack's daxpy gets them once inlined into
; dgefa: the rows cannot overlap in the loop's 100 iterations, so its
; accesses get noalias metadata in place, with no copy and no checks. The
; load and store of y share a scope.
define void @rows(i64 %k) {
; CHECK-LABEL: @rows(
; CHECK-NOT:   lver
; CHECK:       for.body:
; CHECK:         load float, ptr {{%.*}}, align 4, !alias.scope [[XS:![0-9]+]], !noalias [[YS:![0-9]+]]
; CHECK:         load float, ptr {{%.*}}, align 4, !alias.scope [[YS]], !noalias [[XS]]
; CHECK:         store float {{%.*}}, ptr {{%.*}}, align 4, !alias.scope [[YS]], !noalias [[XS]]
; CHECK:       ret void
entry:
  %row = mul nuw nsw i64 %k, 200
  %x = getelementptr inbounds float, ptr @a, i64 %row
  %y = getelementptr inbounds float, ptr %x, i64 100
  br label %for.body

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %vx = load float, ptr %px, align 4
  %py = getelementptr inbounds float, ptr %y, i64 %i
  %vy = load float, ptr %py, align 4
  %m = fmul float %vx, 2.0
  %s = fadd float %m, %vy
  store float %s, ptr %py, align 4
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, 100
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; Here y is one element past x, so each iteration loads what the one before
; stored, and nothing is marked.
define void @overlap(i64 %k) {
; CHECK-LABEL: @overlap(
; CHECK-NOT:   !noalias
; CHECK:       ret void
entry:
  %row = mul nuw nsw i64 %k, 200
  %x = getelementptr inbounds float, ptr @a, i64 %row
  %y = getelementptr inbounds float, ptr %x, i64 1
  br label %for.body

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %vx = load float, ptr %px, align 4
  %py = getelementptr inbounds float, ptr %y, i64 %i
  %vy = load float, ptr %py, align 4
  %m = fmul float %vx, 2.0
  %s = fadd float %m, %vy
  store float %s, ptr %py, align 4
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, 100
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}
