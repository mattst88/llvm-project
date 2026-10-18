; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning | FileCheck %s
; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning -ia64-alias-versioning-max-size=7 \
; RUN:   | FileCheck %s --check-prefix=SMALL
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   -print-pipeline-passes -disable-output < %s \
; RUN:   | FileCheck %s --check-prefix=O2
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O1>' \
; RUN:   -print-pipeline-passes -disable-output < %s \
; RUN:   | FileCheck %s --check-prefix=O1
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   -print-pipeline-passes -disable-output -ia64-alias-versioning=false \
; RUN:   < %s | FileCheck %s --check-prefix=O1

; The pass runs at O2 and above, before the vectorizer and the unroller.
; O2:     ia64-loop-alias-versioning
; O2-SAME: loop-vectorize
; O2-SAME: loop-unroll
; O1-NOT: ia64-loop-alias-versioning

; y may overlap x, so the loop is versioned on whether it does. The copy that
; runs when they do not has noalias metadata, and the other is marked so that
; it is not versioned again. The loop is put in simplified form first.
define void @axpy(ptr %y, ptr %x, double %a, i64 %n) {
; CHECK-LABEL: @axpy(
; CHECK:       for.body.lver.check:
; CHECK:         [[CONFLICT:%.*]] = and i1
; CHECK-NEXT:    br i1 [[CONFLICT]], label %for.body.ph.lver.orig, label %for.body.ph
; CHECK:       for.body.lver.orig:
; CHECK:         load double, ptr {{%.*}}, align 8{{$}}
; CHECK:         br i1 {{%.*}}, label {{%.*}}, label %for.body.lver.orig, !llvm.loop [[ORIG:![0-9]+]]
; CHECK:       for.body:
; CHECK:         load double, ptr {{%.*}}, align 8, !alias.scope [[XS:![0-9]+]]{{$}}
; CHECK:         load double, ptr {{%.*}}, align 8, !alias.scope [[YS:![0-9]+]], !noalias [[XS]]
; CHECK:         store double {{%.*}}, ptr {{%.*}}, align 8, !alias.scope [[YS]], !noalias [[XS]]
; SMALL-LABEL: @axpy(
; SMALL-NOT:   lver
; SMALL:       ret void
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %vx = load double, ptr %px, align 8
  %py = getelementptr inbounds double, ptr %y, i64 %i
  %vy = load double, ptr %py, align 8
  %m = fmul double %a, %vx
  %s = fadd double %m, %vy
  store double %s, ptr %py, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; Pointers that cannot overlap need no checks.
define void @axpy_noalias(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
; CHECK-LABEL: @axpy_noalias(
; CHECK-NOT:   lver
; CHECK:       ret void
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %vx = load double, ptr %px, align 8
  %py = getelementptr inbounds double, ptr %y, i64 %i
  %vy = load double, ptr %py, align 8
  %m = fmul double %a, %vx
  %s = fadd double %m, %vy
  store double %s, ptr %py, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; A loop that optimizes for size is not copied.
define void @axpy_optsize(ptr %y, ptr %x, double %a, i64 %n) optsize {
; CHECK-LABEL: @axpy_optsize(
; CHECK-NOT:   lver
; CHECK:       ret void
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %vx = load double, ptr %px, align 8
  %py = getelementptr inbounds double, ptr %y, i64 %i
  %vy = load double, ptr %py, align 8
  %m = fmul double %a, %vx
  %s = fadd double %m, %vy
  store double %s, ptr %py, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body

exit:
  ret void
}

; The copy of an earlier versioning is not versioned again.
define void @axpy_versioned(ptr %y, ptr %x, double %a, i64 %n) {
; CHECK-LABEL: @axpy_versioned(
; CHECK-NOT:   lver
; CHECK:       ret void
entry:
  %cmp = icmp sgt i64 %n, 0
  br i1 %cmp, label %for.body, label %exit

for.body:
  %i = phi i64 [ %inc, %for.body ], [ 0, %entry ]
  %px = getelementptr inbounds double, ptr %x, i64 %i
  %vx = load double, ptr %px, align 8
  %py = getelementptr inbounds double, ptr %y, i64 %i
  %vy = load double, ptr %py, align 8
  %m = fmul double %a, %vx
  %s = fadd double %m, %vy
  store double %s, ptr %py, align 8
  %inc = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %inc, %n
  br i1 %done, label %exit, label %for.body, !llvm.loop !0

exit:
  ret void
}

; The marker on the copy in @axpy.
; CHECK:       [[ORIG]] = distinct !{[[ORIG]], [[MARK:![0-9]+]]}
; CHECK:       [[MARK]] = !{!"llvm.loop.ia64.alias.versioned", i32 0}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.ia64.alias.versioned"}
