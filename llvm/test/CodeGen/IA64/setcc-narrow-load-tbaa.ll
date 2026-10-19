; RUN: llc -mtriple=ia64-unknown-linux-gnu -stop-after=finalize-isel < %s | FileCheck %s

; Testing bits of a loaded word for zero loads only the byte they are in. The
; narrower load keeps the type of the word it was, so that it is still known
; not to alias a pointer.

; CHECK-LABEL: name: f
; CHECK: ST8 {{.*}} :: (store (s64) into %ir.q, !tbaa !{{[0-9]+}})
; CHECK: LD1 {{.*}} :: (load (s8) from %ir.p, align 4, !tbaa !{{[0-9]+}})
define void @f(ptr %p, ptr %q, ptr %v) {
  store ptr %v, ptr %q, align 8, !tbaa !1
  %w = load i32, ptr %p, align 4, !tbaa !5
  %m = and i32 %w, 12
  %z = icmp eq i32 %m, 0
  br i1 %z, label %e, label %t
t:
  call void @g()
  br label %e
e:
  ret void
}

declare void @g()

!0 = !{!"tbaa root"}
!1 = !{!2, !2, i64 0}
!2 = !{!"any pointer", !3, i64 0}
!3 = !{!"omnipotent char", !0, i64 0}
!4 = !{!"int", !3, i64 0}
!5 = !{!4, !4, i64 0}
