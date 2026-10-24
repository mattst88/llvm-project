; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-defer-math-errno | FileCheck %s
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   -print-pipeline-passes -disable-output < %s \
; RUN:   | FileCheck %s --check-prefix=O2
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O1>' \
; RUN:   -print-pipeline-passes -disable-output < %s \
; RUN:   | FileCheck %s --check-prefix=O1

; The pass runs at O2 and above, before alias versioning, and LICM after it
; hoists the loads that the calls kept in the loop.
; O2:      ia64-defer-math-errno
; O2-SAME: licm
; O2-SAME: ia64-loop-alias-versioning
; O1-NOT:  ia64-defer-math-errno

; The floats are of the size of errno, but their type tells them apart from it.
; Nothing else in the loop can be errno, so the calls become the
; intrinsic, and a call after the loop sets errno if any argument was negative.
define void @sqrts(ptr %d, ptr %s, ptr %f, i64 %n) {
; CHECK-LABEL: @sqrts(
; CHECK:       loop:
; CHECK-NEXT:    [[FAILED:%.*]] = phi i1 [ false, %entry ], [ [[NEXT:%.*]], %loop ]
; CHECK:         [[X:%.*]] = load double
; CHECK-NEXT:    [[XNEG:%.*]] = fcmp olt double [[X]], 0.000000e+00
; CHECK-NEXT:    [[OR:%.*]] = or i1 [[FAILED]], [[XNEG]]
; CHECK-NEXT:    %r = call double @llvm.sqrt.f64(double [[X]])
; CHECK:         [[Y:%.*]] = load float
; CHECK-NEXT:    [[YNEG:%.*]] = fcmp olt float [[Y]], 0.000000e+00
; CHECK-NEXT:    [[NEXT]] = or i1 [[OR]], [[YNEG]]
; CHECK-NEXT:    %ry = call float @llvm.sqrt.f32(float [[Y]])
; CHECK-NOT:     call {{.*}} @sqrt
; CHECK:       exit:
; CHECK-NEXT:    [[LCSSA:%.*]] = phi i1 [ [[NEXT]], %loop ]
; CHECK-NEXT:    br i1 [[LCSSA]], label %[[SET:.*]], label %[[DONE:.*]], !prof [[UNLIKELY:![0-9]+]]
; CHECK:       [[SET]]:
; CHECK-NEXT:    call double @sqrt(double -1.000000e+00)
; CHECK-NEXT:    br label %[[DONE]]
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x)
  %dp = getelementptr double, ptr %d, i64 %i
  store double %r, ptr %dp, align 8
  %fp = getelementptr float, ptr %f, i64 %i
  %y = load float, ptr %fp, align 4, !tbaa !0
  %ry = call float @sqrtf(float %y)
  store float %ry, ptr %fp, align 4, !tbaa !0
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; The stride is an integer and may be errno, so the loop is versioned on
; whether it is. The copy that runs when it may be keeps its call and is
; marked so that it is not versioned again.
define void @strided(ptr %d, ptr %s, ptr %stridep, i64 %n) {
; CHECK-LABEL: @strided(
; CHECK:         [[LOC:%.*]] = call ptr @__errno_location()
; CHECK-NEXT:    [[ERRNO:%.*]] = ptrtoint ptr [[LOC]] to i64
; CHECK-NEXT:    [[ERRNOEND:%.*]] = add i64 [[ERRNO]], 4
; CHECK-NEXT:    [[P:%.*]] = ptrtoint ptr %stridep to i64
; CHECK-NEXT:    [[PEND:%.*]] = add i64 [[P]], 4
; CHECK-NEXT:    [[BEFORE:%.*]] = icmp ule i64 [[PEND]], [[ERRNO]]
; CHECK-NEXT:    [[AFTER:%.*]] = icmp ule i64 [[ERRNOEND]], [[P]]
; CHECK-NEXT:    %errno.apart = or i1 [[BEFORE]], [[AFTER]]
; CHECK-NEXT:    br i1 %errno.apart, label %loop.ph, label %loop.ph.errno.orig
; CHECK:       loop.errno.orig:
; CHECK:         call double @sqrt(double
; CHECK:         br i1 {{%.*}}, label {{%.*}}, label %loop.errno.orig, !llvm.loop [[ORIG:![0-9]+]]
; CHECK:       loop:
; CHECK:         call double @llvm.sqrt.f64(
; CHECK:         br i1 %done, label %[[EXIT:.*]], label %loop
; CHECK:       [[EXIT]]:
; CHECK-NEXT:    [[LCSSA:%.*]] = phi i1
; CHECK-NEXT:    br i1 [[LCSSA]], label %[[SET:.*]], label
; CHECK:       [[SET]]:
; CHECK-NEXT:    call double @sqrt(double -1.000000e+00)
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %st = load i32, ptr %stridep, align 4
  %st64 = sext i32 %st to i64
  %idx = mul nsw i64 %i, %st64
  %sp = getelementptr double, ptr %s, i64 %idx
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x)
  %dp = getelementptr double, ptr %d, i64 %i
  store double %r, ptr %dp, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; An integer access whose address changes may be errno on some pass.
define void @int_varying(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: @int_varying(
; CHECK-NOT:     llvm.sqrt
; CHECK:         call double @sqrt(double %x)
; CHECK-NOT:     llvm.sqrt
; CHECK:         ret void
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x)
  %c = fptosi double %r to i32
  %dp = getelementptr i32, ptr %d, i64 %i
  store i32 %c, ptr %dp, align 4
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; Another call may read errno.
define void @other_call(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: @other_call(
; CHECK-NOT:     llvm.sqrt
; CHECK:         call double @sqrt(double %x)
; CHECK-NOT:     llvm.sqrt
; CHECK:         ret void
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x)
  %dp = getelementptr double, ptr %d, i64 %i
  store double %r, ptr %dp, align 8
  call void @g()
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; A call that may not return would leave errno unset after a failed sqrt.
define void @may_not_return(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: @may_not_return(
; CHECK-NOT:     llvm.sqrt
; CHECK:         call double @sqrt(double %x)
; CHECK-NOT:     llvm.sqrt
; CHECK:         ret void
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x)
  %dp = getelementptr double, ptr %d, i64 %i
  store double %r, ptr %dp, align 8
  call void @h(double %r)
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

define void @strict(ptr %d, ptr %s, i64 %n) strictfp {
; CHECK-LABEL: @strict(
; CHECK-NOT:     llvm.sqrt
; CHECK:         call double @sqrt(double %x)
; CHECK-NOT:     llvm.sqrt
; CHECK:         ret void
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %r = call double @sqrt(double %x) strictfp
  %dp = getelementptr double, ptr %d, i64 %i
  store double %r, ptr %dp, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; CHECK: [[UNLIKELY]] = !{!"branch_weights", i32 1, i32 1048575}
; CHECK: [[ORIG]] = distinct !{[[ORIG]], [[ORIGMD:![0-9]+]]}
; CHECK: [[ORIGMD]] = !{!"llvm.loop.ia64.errno.versioned", i32 0}

declare double @sqrt(double) #0
declare float @sqrtf(float) #0
declare void @g()
declare void @h(double) nounwind memory(none)

attributes #0 = { mustprogress nofree nounwind willreturn memory(errnomem: write) }

!0 = !{!1, !1, i64 0}
!1 = !{!"float", !2, i64 0}
!2 = !{!"omnipotent char", !3, i64 0}
!3 = !{!"Simple C/C++ TBAA"}
!4 = !{!5, !5, i64 0}
!5 = !{!"int", !2, i64 0}
!llvm.errno.tbaa = !{!4}
