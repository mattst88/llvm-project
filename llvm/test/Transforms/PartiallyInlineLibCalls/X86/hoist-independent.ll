; RUN: opt -S -passes=partially-inline-libcalls -mtriple=x86_64-unknown-linux-gnu < %s | FileCheck %s

; The address of the store does not depend on the sqrt, so it is computed
; before the check of the native sqrt's result rather than after it. The
; load, what uses the load, the division that may trap and what uses the sqrt
; stay after it.
define void @f(double %x, ptr %p, i64 %i, i64 %n, ptr %q) {
; CHECK-LABEL: @f(
; CHECK:         [[MUL:%.*]] = mul nsw i64 %i, 3
; CHECK-NEXT:    [[ADD:%.*]] = add nsw i64 [[MUL]], %n
; CHECK-NEXT:    [[GEP:%.*]] = getelementptr inbounds double, ptr %p, i64 [[ADD]]
; CHECK-NEXT:    [[SQRT:%.*]] = call double @sqrt(double %x)
; CHECK-NEXT:    [[ORD:%.*]] = fcmp oge double %x, 0.000000e+00
; CHECK-NEXT:    br i1 [[ORD]], label %[[SPLIT:.*]], label %call.sqrt
; CHECK:       [[SPLIT]]:
; CHECK-NEXT:    [[PHI:%.*]] = phi double [ [[SQRT]], %{{.*}} ], [ %{{.*}}, %call.sqrt ]
; CHECK-NEXT:    [[LOAD:%.*]] = load i64, ptr %q
; CHECK-NEXT:    [[OFF:%.*]] = add i64 [[LOAD]], 1
; CHECK-NEXT:    [[DIV:%.*]] = udiv i64 %i, %n
; CHECK-NEXT:    [[USE:%.*]] = fmul double [[PHI]], 2.000000e+00
; CHECK-NEXT:    store double [[USE]], ptr [[GEP]]
  %s = call double @sqrt(double %x)
  %mul = mul nsw i64 %i, 3
  %add = add nsw i64 %mul, %n
  %gep = getelementptr inbounds double, ptr %p, i64 %add
  %load = load i64, ptr %q
  %off = add i64 %load, 1
  %div = udiv i64 %i, %n
  %use = fmul double %s, 2.0
  store double %use, ptr %gep
  store i64 %off, ptr %q
  store i64 %div, ptr %p
  ret void
}

declare double @sqrt(double)
