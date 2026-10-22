; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   | FileCheck %s
; RUN: opt < %s -S -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   -ia64-alias-versioning=false | FileCheck %s --check-prefix=OFF
; RUN: opt -mtriple=ia64-unknown-linux-gnu -passes='default<O2>' \
; RUN:   -print-pipeline-passes -disable-output < %s \
; RUN:   | FileCheck %s --check-prefix=PIPE

; After versioning, the loops are cleaned up again before the vectorizer.
; PIPE: ia64-loop-alias-versioning,loop-mssa(licm<allowspeculation>,loop-idiom,loop-deletion)

; The byte stores may overwrite the count, so the loop reloads it each
; iteration and has no trip count. The copy that runs when they do not has
; the load hoisted out of it, and becomes a memmove.
define void @drop(ptr %pg, ptr %ptr, i32 %idx) {
; CHECK-LABEL: @drop(
; CHECK:       for.body.lver.orig:
; CHECK:         load i16, ptr %pg
; CHECK:         br i1 {{%.*}}, label %for.body.lver.orig, label %exit
; CHECK:       for.body.ph:
; CHECK:         call void @llvm.memmove.p0.p0.i64(ptr {{.*}}%ptr, ptr {{.*}}, i64 {{%.*}}, i1 false)
; CHECK-NEXT:    br label %exit
; OFF-LABEL: @drop(
; OFF-NOT:     @llvm.memmove
entry:
  %first = add nsw i32 %idx, 1
  %n0 = load i16, ptr %pg, align 2
  %n0.ext = zext i16 %n0 to i32
  %any = icmp slt i32 %first, %n0.ext
  br i1 %any, label %for.body, label %exit

for.body:
  %i = phi i32 [ %first, %entry ], [ %i.next, %for.body ]
  %p = phi ptr [ %ptr, %entry ], [ %p2, %for.body ]
  %p2 = getelementptr inbounds i8, ptr %p, i64 2
  %v0 = load i8, ptr %p2, align 1
  store i8 %v0, ptr %p, align 1
  %p3 = getelementptr inbounds i8, ptr %p, i64 3
  %v1 = load i8, ptr %p3, align 1
  %p1 = getelementptr inbounds i8, ptr %p, i64 1
  store i8 %v1, ptr %p1, align 1
  %i.next = add nsw i32 %i, 1
  %n = load i16, ptr %pg, align 2
  %n.ext = zext i16 %n to i32
  %more = icmp slt i32 %i.next, %n.ext
  br i1 %more, label %for.body, label %exit

exit:
  ret void
}
