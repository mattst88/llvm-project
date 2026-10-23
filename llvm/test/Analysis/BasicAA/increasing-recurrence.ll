; RUN: opt -passes=aa-eval -print-all-alias-modref-info -disable-output < %s 2>&1 | FileCheck %s

; The n-body loop: bodies[i] is updated while walking the bodies[j] after it.
; j starts at i + 1 and only grows, so the two elements never overlap and the
; update of bodies[i] can stay in a register across the inner loop.

; CHECK-LABEL: Function: nbody
; CHECK: NoAlias: double* %bi, double* %bj
define void @nbody(ptr %bodies, i32 %n) {
entry:
  br label %outer

outer:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %i.ext = zext nneg i32 %i to i64
  %bi = getelementptr inbounds [56 x i8], ptr %bodies, i64 %i.ext, i64 24
  %i.next = add nuw nsw i32 %i, 1
  br label %inner

inner:
  %j = phi i32 [ %i.next, %outer ], [ %j.next, %inner ]
  %j.ext = zext nneg i32 %j to i64
  %bj = getelementptr inbounds [56 x i8], ptr %bodies, i64 %j.ext, i64 24
  %vi = load double, ptr %bi
  %vj = load double, ptr %bj
  %sum = fadd double %vi, %vj
  store double %sum, ptr %bi
  store double %sum, ptr %bj
  %j.next = add nuw nsw i32 %j, 1
  %done = icmp eq i32 %j.next, %n
  br i1 %done, label %outer.latch, label %inner

outer.latch:
  %outer.done = icmp eq i32 %i.next, %n
  br i1 %outer.done, label %exit, label %outer

exit:
  ret void
}

; Starting j at i itself gives no such guarantee.

; CHECK-LABEL: Function: same_start
; CHECK: MayAlias: double* %bi, double* %bj
define void @same_start(ptr %bodies, i32 %n) {
entry:
  br label %outer

outer:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %i.ext = zext nneg i32 %i to i64
  %bi = getelementptr inbounds [56 x i8], ptr %bodies, i64 %i.ext, i64 24
  %i.next = add nuw nsw i32 %i, 1
  br label %inner

inner:
  %j = phi i32 [ %i, %outer ], [ %j.next, %inner ]
  %j.ext = zext nneg i32 %j to i64
  %bj = getelementptr inbounds [56 x i8], ptr %bodies, i64 %j.ext, i64 24
  %vi = load double, ptr %bi
  %vj = load double, ptr %bj
  %sum = fadd double %vi, %vj
  store double %sum, ptr %bi
  store double %sum, ptr %bj
  %j.next = add nuw nsw i32 %j, 1
  %done = icmp eq i32 %j.next, %n
  br i1 %done, label %outer.latch, label %inner

outer.latch:
  %outer.done = icmp eq i32 %i.next, %n
  br i1 %outer.done, label %exit, label %outer

exit:
  ret void
}
