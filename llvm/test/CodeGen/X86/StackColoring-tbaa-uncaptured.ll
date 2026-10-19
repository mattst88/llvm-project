; RUN: llc -mtriple=x86_64-unknown-linux-gnu -stop-after=stack-coloring %s -o - | FileCheck %s

; %a and %b share a slot. A pointer passed in as an argument cannot point into
; it, and one loaded from memory can only if its address escapes, so accesses
; through such pointers keep their TBAA tags while accesses to the slot itself
; lose them.

; CHECK-LABEL: name: uncaptured
; CHECK:       stack:
; CHECK-NEXT:    - { id: 0, name: a,
; CHECK-NOT:     - { id: 1
; CHECK:       body:
; CHECK:       (store (s32) into %ir.pa)
; CHECK:       (store (s32) into %ir.pb)
; CHECK:       (load (s16) from %ir.p, !tbaa ![[SHORT:[0-9]+]])
; CHECK:       (load (s64) from %ir.pp, !tbaa ![[ANY:[0-9]+]])
; CHECK:       (load (s16) from %ir.q, !tbaa ![[SHORT]])
define i32 @uncaptured(ptr %p, ptr %pp, i64 %i) {
  %a = alloca [16 x i32], align 4
  %b = alloca [16 x i32], align 4
  call void @llvm.lifetime.start.p0(ptr %a)
  %pa = getelementptr [4 x i8], ptr %a, i64 %i
  store i32 1, ptr %pa, align 4, !tbaa !0
  call void @llvm.lifetime.end.p0(ptr %a)
  call void @llvm.lifetime.start.p0(ptr %b)
  %pb = getelementptr [4 x i8], ptr %b, i64 %i
  store i32 2, ptr %pb, align 4, !tbaa !0
  call void @llvm.lifetime.end.p0(ptr %b)
  %x = load i16, ptr %p, align 2, !tbaa !3
  %q = load ptr, ptr %pp, align 8, !tbaa !5
  %y = load i16, ptr %q, align 2, !tbaa !3
  %s = add i16 %x, %y
  %r = zext i16 %s to i32
  ret i32 %r
}

; Here %a escapes, so %q may point into the shared slot, while %p still
; cannot.

; CHECK-LABEL: name: captured
; CHECK:       stack:
; CHECK-NEXT:    - { id: 0, name: a,
; CHECK-NOT:     - { id: 1
; CHECK:       body:
; CHECK:       (load (s16) from %ir.p, !tbaa
; CHECK:       (load (s16) from %ir.q)
define i32 @captured(ptr %p, ptr %pp, ptr %out, i64 %i) {
  %a = alloca [16 x i32], align 4
  %b = alloca [16 x i32], align 4
  call void @llvm.lifetime.start.p0(ptr %a)
  store ptr %a, ptr %out, align 8
  %pa = getelementptr [4 x i8], ptr %a, i64 %i
  store i32 1, ptr %pa, align 4, !tbaa !0
  call void @llvm.lifetime.end.p0(ptr %a)
  call void @llvm.lifetime.start.p0(ptr %b)
  %pb = getelementptr [4 x i8], ptr %b, i64 %i
  store i32 2, ptr %pb, align 4, !tbaa !0
  call void @llvm.lifetime.end.p0(ptr %b)
  %x = load i16, ptr %p, align 2, !tbaa !3
  %q = load ptr, ptr %pp, align 8, !tbaa !5
  %y = load i16, ptr %q, align 2, !tbaa !3
  %s = add i16 %x, %y
  %r = zext i16 %s to i32
  ret i32 %r
}

; The lookup of underlying objects gives up within this chain of GEPs, which
; still points into the shared slot.

; CHECK-LABEL: name: deep
; CHECK:       stack:
; CHECK-NEXT:    - { id: 0, name: a,
; CHECK-NOT:     - { id: 1
; CHECK:       body:
; CHECK:       (load (s16) from %ir.g12)
define i32 @deep(i64 %i) {
  %a = alloca [16 x i32], align 4
  %b = alloca [16 x i32], align 4
  call void @llvm.lifetime.start.p0(ptr %a)
  %pa = getelementptr [4 x i8], ptr %a, i64 %i
  store i32 1, ptr %pa, align 4, !tbaa !0
  call void @llvm.lifetime.end.p0(ptr %a)
  call void @llvm.lifetime.start.p0(ptr %b)
  %pb = getelementptr [4 x i8], ptr %b, i64 %i
  store i32 2, ptr %pb, align 4, !tbaa !0
  %g1 = getelementptr i8, ptr %b, i64 %i
  %g2 = getelementptr i8, ptr %g1, i64 %i
  %g3 = getelementptr i8, ptr %g2, i64 %i
  %g4 = getelementptr i8, ptr %g3, i64 %i
  %g5 = getelementptr i8, ptr %g4, i64 %i
  %g6 = getelementptr i8, ptr %g5, i64 %i
  %g7 = getelementptr i8, ptr %g6, i64 %i
  %g8 = getelementptr i8, ptr %g7, i64 %i
  %g9 = getelementptr i8, ptr %g8, i64 %i
  %g10 = getelementptr i8, ptr %g9, i64 %i
  %g11 = getelementptr i8, ptr %g10, i64 %i
  %g12 = getelementptr i8, ptr %g11, i64 %i
  %x = load i16, ptr %g12, align 2, !tbaa !3
  call void @llvm.lifetime.end.p0(ptr %b)
  %r = zext i16 %x to i32
  ret i32 %r
}

declare void @llvm.lifetime.start.p0(ptr)
declare void @llvm.lifetime.end.p0(ptr)

!0 = !{!1, !1, i64 0}
!1 = !{!"int", !2, i64 0}
!2 = !{!"Simple C TBAA"}
!3 = !{!4, !4, i64 0}
!4 = !{!"short", !2, i64 0}
!5 = !{!6, !6, i64 0}
!6 = !{!"any pointer", !2, i64 0}
