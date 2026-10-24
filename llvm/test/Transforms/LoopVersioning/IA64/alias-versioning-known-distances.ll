; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning | FileCheck %s
; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning \
; RUN:   -ia64-alias-versioning-known-distances=false \
; RUN:   | FileCheck %s --check-prefix=OFF

; Each pass reads the element of %r that the one before it wrote, which keeps
; the vectorizer away, but is a known distance apart. The loop is still
; versioned on %r overlapping the rows above and below it. The accesses to %r
; keep one scope, so their order stays.
define void @stencil(ptr %r, ptr %u, ptr %d, double %a, i64 %n) {
; CHECK-LABEL: @stencil(
; CHECK:       for.body.lver.check:
; CHECK:       for.body.lver.orig:
; CHECK:       for.body:
; CHECK:         load double, ptr {{%.*}}, align 8, !alias.scope [[US:![0-9]+]]{{$}}
; CHECK:         load double, ptr {{%.*}}, align 8, !alias.scope [[DS:![0-9]+]]{{$}}
; CHECK:         load double, ptr {{%.*}}, align 8, !alias.scope [[RS:![0-9]+]], !noalias [[NA:![0-9]+]]
; CHECK:         store double {{%.*}}, ptr {{%.*}}, align 8, !alias.scope [[RS]], !noalias [[NA]]
; OFF-LABEL:   @stencil(
; OFF-NOT:     lver
; OFF:         ret void
entry:
  %cmp = icmp sgt i64 %n, 1
  br i1 %cmp, label %for.body, label %exit

for.body:
  %j = phi i64 [ %inc, %for.body ], [ 1, %entry ]
  %pu = getelementptr inbounds double, ptr %u, i64 %j
  %vu = load double, ptr %pu, align 8
  %pd = getelementptr inbounds double, ptr %d, i64 %j
  %vd = load double, ptr %pd, align 8
  %prev = getelementptr inbounds double, ptr %r, i64 %j
  %pp = getelementptr inbounds i8, ptr %prev, i64 -8
  %vp = load double, ptr %pp, align 8
  %s1 = fadd double %vu, %vd
  %s2 = fadd double %s1, %vp
  %m = fmul double %a, %s2
  store double %m, ptr %prev, align 8
  %inc = add nuw nsw i64 %j, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; An access whose bounds are not known cannot be checked.
define void @stencil_unknown(ptr %r, ptr %u, double %a, i64 %k, i64 %n) {
; CHECK-LABEL: @stencil_unknown(
; CHECK-NOT:   lver
; CHECK:       ret void
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %j = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %pu = getelementptr inbounds double, ptr %u, i64 %j
  %vu = load double, ptr %pu, align 8
  %jk = urem i64 %j, %k
  %pk = getelementptr inbounds double, ptr %r, i64 %jk
  %vk = load double, ptr %pk, align 8
  %s = fadd double %vu, %vk
  %m = fmul double %a, %s
  %pr = getelementptr inbounds double, ptr %r, i64 %j
  store double %m, ptr %pr, align 8
  %inc = add nuw nsw i64 %j, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}
