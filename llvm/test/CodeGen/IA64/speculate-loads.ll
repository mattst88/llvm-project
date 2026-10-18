; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The index is loaded, so t[i] is likely to miss. It is issued with ld4.sa,
; together with the shladd computing its address, above both the bounds check
; and the store that may write it. ld4.c.clr stands where it was.
define i32 @chase(ptr %t, ptr %p, ptr %q, i64 %lim) {
; CHECK-LABEL: chase:
; CHECK:       ld8 [[I:r[0-9]+]] = [r33]
; CHECK:       shladd [[A:r[0-9]+]] = [[I]], 2, r32
; CHECK:       ld4.sa [[V:r[0-9]+]] = [[[A]]]
; CHECK:       br.cond
; CHECK:       st4 [r34] =
; CHECK:       ld4.c.clr [[V]] = [[[A]]]
entry:
  %i = load i64, ptr %p
  %c = icmp ult i64 %i, %lim
  br i1 %c, label %in, label %out, !prof !0
in:
  store i32 0, ptr %q
  %a = getelementptr i32, ptr %t, i64 %i
  %v = load i32, ptr %a
  %r = add i32 %v, 1
  ret i32 %r
out:
  ret i32 -1
}

; An address from an argument is not a chase: the load stays put.
define i32 @nochase(ptr %t, ptr %q, i64 %i, i64 %lim) {
; CHECK-LABEL: nochase:
; CHECK-NOT:   .sa
; CHECK-NOT:   .c.clr
; CHECK:       .endp nochase
entry:
  %c = icmp ult i64 %i, %lim
  br i1 %c, label %in, label %out, !prof !0
in:
  store i32 0, ptr %q
  %a = getelementptr i32, ptr %t, i64 %i
  %v = load i32, ptr %a
  %r = add i32 %v, 1
  ret i32 %r
out:
  ret i32 -1
}

; A volatile load stays put.
define i32 @volatile(ptr %t, ptr %p, ptr %q, i64 %lim) {
; CHECK-LABEL: volatile:
; CHECK-NOT:   .sa
; CHECK-NOT:   .c.clr
; CHECK:       .endp volatile
entry:
  %i = load i64, ptr %p
  %c = icmp ult i64 %i, %lim
  br i1 %c, label %in, label %out, !prof !0
in:
  store i32 0, ptr %q
  %a = getelementptr i32, ptr %t, i64 %i
  %v = load volatile i32, ptr %a
  %r = add i32 %v, 1
  ret i32 %r
out:
  ret i32 -1
}

; A load the guard usually skips is not worth issuing early.
define i32 @rare(ptr %t, ptr %p, ptr %q, i64 %lim) {
; CHECK-LABEL: rare:
; CHECK-NOT:   .sa
; CHECK-NOT:   .c.clr
; CHECK:       .endp rare
entry:
  %i = load i64, ptr %p
  %c = icmp ult i64 %i, %lim
  br i1 %c, label %in, label %out, !prof !1
in:
  store i32 0, ptr %q
  %a = getelementptr i32, ptr %t, i64 %i
  %v = load i32, ptr %a
  %r = add i32 %v, 1
  ret i32 %r
out:
  ret i32 -1
}

; The address of the last load is written back by a post-increment load. It
; steps through memory and is not a chase.
define i64 @walk(ptr %p, ptr %q, i64 %x) {
; CHECK-LABEL: walk:
; CHECK-NOT:   .sa
; CHECK-NOT:   .c.clr
; CHECK:       .endp walk
entry:
  %a = load i64, ptr %p
  %p1 = getelementptr i64, ptr %p, i64 1
  %b = load i64, ptr %p1
  %p2 = getelementptr i64, ptr %p, i64 2
  %s = add i64 %a, %b
  %t = mul i64 %s, %x
  store i64 %t, ptr %q
  %c = load i64, ptr %p2
  %r = add i64 %c, %t
  ret i64 %r
}

!0 = !{!"branch_weights", i32 100, i32 1}
!1 = !{!"branch_weights", i32 1, i32 100}
