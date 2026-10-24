; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning | FileCheck %s
; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu \
; RUN:   -passes=ia64-loop-alias-versioning \
; RUN:   -ia64-alias-versioning-unit-strides=false \
; RUN:   | FileCheck %s --check-prefix=OFF

; OFF-NOT: lver

; d is stored to with a stride that is not known, which the access analysis
; can only check against s by assuming it is 1. The loop is versioned on that
; as well as on d and s being apart, and the copy that runs when it is 1 uses
; 1 for it.
define void @strided(ptr %d, ptr %s, i64 %st, i64 %n) {
; CHECK-LABEL: @strided(
; CHECK:         %found.conflict = and i1
; CHECK-NEXT:    %ident.check = icmp ne i64 %st, 1
; CHECK-NEXT:    %lver.safe = or i1 %found.conflict, %ident.check
; CHECK-NEXT:    br i1 %lver.safe, label %loop.ph.lver.orig, label %loop.ph
; CHECK:       loop.lver.orig:
; CHECK:         %idx.lver.orig = mul nsw i64 %i.lver.orig, %st
; CHECK:       loop:
; CHECK:         %idx = mul nsw i64 %i, 1
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %y = fmul double %x, 2.0
  %idx = mul nsw i64 %i, %st
  %dp = getelementptr inbounds double, ptr %d, i64 %idx
  store double %y, ptr %dp, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; The stride is used through a sign extension of it.
define void @strided_i32(ptr %d, ptr %s, i32 %st, i64 %n) {
; CHECK-LABEL: @strided_i32(
; CHECK:         %ident.check = icmp ne i32 %st, 1
; CHECK:       loop.lver.orig:
; CHECK:         %idx.lver.orig = mul nsw i64 %i.lver.orig, %st64
; CHECK:       loop:
; CHECK:         %idx = mul nsw i64 %i, 1
entry:
  %st64 = sext i32 %st to i64
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds double, ptr %s, i64 %i
  %x = load double, ptr %sp, align 8
  %y = fmul double %x, 2.0
  %idx = mul nsw i64 %i, %st64
  %dp = getelementptr inbounds double, ptr %d, i64 %idx
  store double %y, ptr %dp, align 8
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
