; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
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

; The bytes are not chases, so they are loaded where they were, but the
; addresses of the second pair are computed before the branch, beside the first
; pair's.
define i32 @early_address(ptr %p, i32 %a, i32 %b) {
; CHECK-LABEL: early_address:
; CHECK-DAG:   adds {{r[0-9]+}} = 1, r33
; CHECK-DAG:   adds {{r[0-9]+}} = 1, r34
; CHECK:       br.ret.dptk.many b0 ;;
; CHECK-NEXT:  .LBB{{[0-9]+}}_1:
; CHECK-NEXT:  ld1
entry:
  %za = zext i32 %a to i64
  %pa = getelementptr i8, ptr %p, i64 %za
  %x = load i8, ptr %pa
  %zb = zext i32 %b to i64
  %pb = getelementptr i8, ptr %p, i64 %zb
  %y = load i8, ptr %pb
  %c = icmp eq i8 %x, %y
  br i1 %c, label %next, label %out
next:
  %a1 = add i32 %a, 1
  %za1 = zext i32 %a1 to i64
  %pa1 = getelementptr i8, ptr %p, i64 %za1
  %x1 = load i8, ptr %pa1
  %b1 = add i32 %b, 1
  %zb1 = zext i32 %b1 to i64
  %pb1 = getelementptr i8, ptr %p, i64 %zb1
  %y1 = load i8, ptr %pb1
  %c1 = icmp ult i8 %x1, %y1
  %r = zext i1 %c1 to i32
  ret i32 %r
out:
  ret i32 -1
}

; Not if the branch usually leaves.
define i32 @rare_address(ptr %p, i32 %a, i32 %b) {
; CHECK-LABEL: rare_address:
; CHECK-NOT:   adds
; CHECK:       .LBB{{[0-9]+}}_1:
; CHECK-NEXT:  adds {{r[0-9]+}} = 1, r33
entry:
  %za = zext i32 %a to i64
  %pa = getelementptr i8, ptr %p, i64 %za
  %x = load i8, ptr %pa
  %zb = zext i32 %b to i64
  %pb = getelementptr i8, ptr %p, i64 %zb
  %y = load i8, ptr %pb
  %c = icmp eq i8 %x, %y
  br i1 %c, label %next, label %out, !prof !1
next:
  %a1 = add i32 %a, 1
  %za1 = zext i32 %a1 to i64
  %pa1 = getelementptr i8, ptr %p, i64 %za1
  %x1 = load i8, ptr %pa1
  %b1 = add i32 %b, 1
  %zb1 = zext i32 %b1 to i64
  %pb1 = getelementptr i8, ptr %p, i64 %zb1
  %y1 = load i8, ptr %pb1
  %c1 = icmp ult i8 %x1, %y1
  %r = zext i1 %c1 to i32
  ret i32 %r
out:
  ret i32 -1
}

; The walk ends at the top of the loop, which the shladd begins. The adds
; moves up to it and stays after it.
define void @shared_address(ptr %t, i64 %n) {
; CHECK-LABEL: shared_address:
; CHECK:       shladd [[E:r[0-9]+]] = {{r[0-9]+}}, 2, r32
; CHECK-NEXT:  and
; CHECK-NEXT:  adds {{r[0-9]+}} = 4, [[E]]
; CHECK-NEXT:  cmp.eq
; CHECK-NEXT:  (p6) br.cond
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ 1, %latch ]
  %e = getelementptr [4 x i8], ptr %t, i64 %i
  %x = and i64 %i, %n
  %c = icmp ne i64 %x, 0
  br i1 %c, label %check, label %latch
check:
  %f = getelementptr i8, ptr %e, i64 4
  %v = load i32, ptr %f
  %pos = icmp sgt i32 %v, 0
  br i1 %pos, label %exit, label %latch
exit:
  ret void
latch:
  store i32 0, ptr %e
  br label %loop
}

; The store's address and value are computed above the likely branch, which
; leaves the store alone in its block to be predicated.
define void @early_store(ptr %p, i32 %a, i64 %v, i64 %t) {
; CHECK-LABEL: early_store:
; CHECK:       adds r14 = 1, r33
; CHECK-NEXT:  adds r15 = 3, r34
; CHECK-NEXT:  cmp.eq p7, p6 = 0, r35 ;;
; CHECK-NEXT:  addp4 r14 = r14, r0 ;;
; CHECK-NEXT:  shladd r14 = r14, 2, r32 ;;
; CHECK-NEXT:  (p7) st4 [r14] = r15
entry:
  %c = icmp eq i64 %t, 0
  br i1 %c, label %next, label %out, !prof !0
next:
  %a1 = add i32 %a, 1
  %za1 = zext i32 %a1 to i64
  %pa1 = getelementptr i32, ptr %p, i64 %za1
  %v1 = trunc i64 %v to i32
  %v2 = add i32 %v1, 3
  store i32 %v2, ptr %pa1
  ret void
out:
  ret void
}

; Behind an unlikely branch they stay put.
define void @rare_store(ptr %p, i32 %a, i64 %v, i64 %t) {
; CHECK-LABEL: rare_store:
; CHECK:       (p6) br.ret.dptk.many b0 ;;
; CHECK-NEXT:  .LBB{{[0-9]+}}_1:
; CHECK-NEXT:  adds r14 = 1, r33
entry:
  %c = icmp eq i64 %t, 0
  br i1 %c, label %next, label %out, !prof !1
next:
  %a1 = add i32 %a, 1
  %za1 = zext i32 %a1 to i64
  %pa1 = getelementptr i32, ptr %p, i64 %za1
  %v1 = trunc i64 %v to i32
  %v2 = add i32 %v1, 3
  store i32 %v2, ptr %pa1
  ret void
out:
  ret void
}

; The second branch's compare operand is computed above the first branch, so
; the two compares combine into one parallel compare.
define void @early_branch(i32 %a, i32 %n, i64 %t) {
; CHECK-LABEL: early_branch:
; CHECK:       shl r14 = r32, 1 ;;
; CHECK:       cmp4.lt p7, p6 = r14, r33 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p6, p7 = 0, r34
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB{{[0-9]+}}_3{{$}}
; CHECK:       br.call.sptk.many b0 = f ;;
entry:
  %c = icmp eq i64 %t, 0
  br i1 %c, label %next, label %out, !prof !0
next:
  %a1 = shl i32 %a, 1
  %c1 = icmp slt i32 %a1, %n
  br i1 %c1, label %x, label %out
x:
  call void @f()
  ret void
out:
  call void @g()
  ret void
}

declare void @f()
declare void @g()

!0 = !{!"branch_weights", i32 100, i32 1}
!1 = !{!"branch_weights", i32 1, i32 100}
