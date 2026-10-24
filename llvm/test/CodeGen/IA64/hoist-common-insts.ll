; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes=ia64-hoist-common-insts -S < %s | FileCheck %s

@dm1 = global double 0.0

; Both arms convert k and scale it, in different orders.
; CHECK-LABEL: @arms
; CHECK: entry:
; CHECK-NEXT: %c = icmp eq i32 %h, 0
; CHECK-NEXT: %d2 = load double, ptr @dm1
; CHECK-NEXT: %conv2 = sitofp i32 %k to double
; CHECK-NEXT: %mul2 = fmul double %d2, %conv2
; CHECK-NEXT: br i1 %c
; CHECK: then:
; CHECK-NEXT: %l = load double, ptr %left
; CHECK-NEXT: %r = fadd double %mul2, %l
; CHECK: else:
; CHECK-NEXT: br label %join
; CHECK: join:
; CHECK-NEXT: phi double [ %r, %then ], [ %mul2, %else ]
define double @arms(ptr %left, i32 %h, i32 %k) {
entry:
  %c = icmp eq i32 %h, 0
  br i1 %c, label %else, label %then
then:
  %l = load double, ptr %left
  %d = load double, ptr @dm1
  %conv = sitofp i32 %k to double
  %mul = fmul double %d, %conv
  %r = fadd double %mul, %l
  br label %join
else:
  %d2 = load double, ptr @dm1
  %conv2 = sitofp i32 %k to double
  %mul2 = fmul double %d2, %conv2
  br label %join
join:
  %v = phi double [ %r, %then ], [ %mul2, %else ]
  ret double %v
}

; A load after a store in one arm stays there.
; CHECK-LABEL: @stored
; CHECK: entry:
; CHECK-NEXT: %c = icmp eq i32 %h, 0
; CHECK-NEXT: br i1 %c
; CHECK: then:
; CHECK-NEXT: store double 0.0
; CHECK-NEXT: %d = load double, ptr @dm1
; CHECK: else:
; CHECK-NEXT: %d2 = load double, ptr @dm1
define double @stored(ptr %p, i32 %h) {
entry:
  %c = icmp eq i32 %h, 0
  br i1 %c, label %else, label %then
then:
  store double 0.0, ptr %p
  %d = load double, ptr @dm1
  br label %join
else:
  %d2 = load double, ptr @dm1
  br label %join
join:
  %v = phi double [ %d, %then ], [ %d2, %else ]
  ret double %v
}

; An arm that other blocks reach keeps its instructions.
; CHECK-LABEL: @shared
; CHECK: pre:
; CHECK-NEXT: %c = icmp eq i32 %h, 0
; CHECK-NEXT: br i1 %c
; CHECK: then:
; CHECK-NEXT: %conv = sitofp
; CHECK: else:
; CHECK-NEXT: %conv2 = sitofp
define double @shared(i32 %h, i32 %k, i1 %b) {
entry:
  br i1 %b, label %pre, label %else
pre:
  %c = icmp eq i32 %h, 0
  br i1 %c, label %else, label %then
then:
  %conv = sitofp i32 %k to double
  br label %join
else:
  %conv2 = sitofp i32 %k to double
  br label %join
join:
  %v = phi double [ %conv, %then ], [ %conv2, %else ]
  ret double %v
}

; A load whose address the branching block computes stays in the arms, where
; it does not hold up the branch.
; CHECK-LABEL: @indexed
; CHECK: entry:
; CHECK-NEXT: %k = sext i32 %i to i64
; CHECK-NEXT: %c = icmp eq i32 %h, 0
; CHECK-NEXT: br i1 %c
; CHECK: then:
; CHECK-NEXT: %p1 = getelementptr
; CHECK-NEXT: %v1 = load double, ptr %p1
; CHECK: else:
; CHECK-NEXT: %p2 = getelementptr
; CHECK-NEXT: %v2 = load double, ptr %p2
define double @indexed(ptr %a, i32 %i, i32 %h) {
entry:
  %k = sext i32 %i to i64
  %c = icmp eq i32 %h, 0
  br i1 %c, label %else, label %then
then:
  %p1 = getelementptr inbounds double, ptr %a, i64 %k
  %v1 = load double, ptr %p1
  %r = fadd double %v1, 1.0
  br label %join
else:
  %p2 = getelementptr inbounds double, ptr %a, i64 %k
  %v2 = load double, ptr %p2
  br label %join
join:
  %v = phi double [ %r, %then ], [ %v2, %else ]
  ret double %v
}

; Integer work gains nothing by starting before the branch.
; CHECK-LABEL: @integer
; CHECK: entry:
; CHECK-NEXT: %c = icmp eq i32 %h, 0
; CHECK-NEXT: br i1 %c
; CHECK: then:
; CHECK-NEXT: %s1 = add i32 %x, %y
; CHECK: else:
; CHECK-NEXT: %s2 = add i32 %x, %y
define i32 @integer(i32 %x, i32 %y, i32 %h) {
entry:
  %c = icmp eq i32 %h, 0
  br i1 %c, label %else, label %then
then:
  %s1 = add i32 %x, %y
  %r = shl i32 %s1, 1
  br label %join
else:
  %s2 = add i32 %x, %y
  br label %join
join:
  %v = phi i32 [ %r, %then ], [ %s2, %else ]
  ret i32 %v
}
