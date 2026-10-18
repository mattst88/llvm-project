; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-packing=false < %s | FileCheck %s --check-prefix=OFF
; The option is not named after the pass, so tools that make an option of
; every pass name start.
; RUN: opt -mtriple=ia64-unknown-linux-gnu -disable-output < %s

; Eight A-unit instructions fill three bundles either way. The assembler
; would choose MII for all three, and two MII bundles have four I slots for
; two I ports, so the second waits a cycle. With MMI first, the first two
; bundles issue together. After the stop, the add that would sit between the
; first two stores goes after the second, so that the stores pair up in MMI
; bundles.
define void @six_adds(ptr %p, i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: six_adds:
; CHECK:       .mmi
; CHECK-NEXT:  adds r14 = 1, r33
; CHECK-NEXT:  adds r15 = 2, r34
; CHECK-NEXT:  adds r18 = 8, r32
; CHECK-NEXT:  adds r16 = 3, r35
; CHECK-NEXT:  add r17 = r33, r34
; CHECK-NEXT:  adds r19 = 16, r32
; CHECK-NEXT:  adds r20 = 24, r32
; CHECK-NEXT:  adds r21 = 32, r32 ;;
; CHECK-NEXT:  st8 [r32] = r14, 40
; CHECK-NEXT:  st8 [r18] = r15
; CHECK-NEXT:  add r15 = r33, r35
; CHECK-NEXT:  st8 [r19] = r16
; CHECK-NEXT:  st8 [r20] = r17
; CHECK-NEXT:  add r14 = r34, r35 ;;
; CHECK-NEXT:  st8 [r21] = r14
; CHECK-NEXT:  st8 [r32] = r15
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; OFF-LABEL: six_adds:
; OFF-NOT:     .mmi
; OFF:         st8 [r32] = r14, 40
; OFF-NEXT:    add r14 = r34, r35
; OFF-NEXT:    st8 [r18] = r15
  %x1 = add i64 %a, 1
  %x2 = add i64 %b, 2
  %x3 = add i64 %c, 3
  %x4 = add i64 %a, %b
  %x5 = add i64 %b, %c
  %x6 = add i64 %a, %c
  %p1 = getelementptr i64, ptr %p, i64 1
  %p2 = getelementptr i64, ptr %p, i64 2
  %p3 = getelementptr i64, ptr %p, i64 3
  %p4 = getelementptr i64, ptr %p, i64 4
  %p5 = getelementptr i64, ptr %p, i64 5
  store i64 %x1, ptr %p
  store i64 %x2, ptr %p1
  store i64 %x3, ptr %p2
  store i64 %x4, ptr %p3
  store i64 %x5, ptr %p4
  store i64 %x6, ptr %p5
  ret void
}

; The frame is saved and the out registers are filled in three bundles. The
; assembler would choose MII for the first two, so the second waits for an I
; port. Neither MMI alone issues them in one cycle, so the templates of the
; two are chosen together. Only port I0 moves from b0, so the third is MMI
; too, which puts that move in its first I slot.
declare void @g(i64, i64, i64, i64, i64, i64)
define void @six_moves(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f) {
; CHECK-LABEL: six_moves:
; CHECK:       .mmi
; CHECK-NEXT:  alloc r39 = ar.pfs, 6, 3, 6, 0
; CHECK-NEXT:  mov r38 = r1
; CHECK-NEXT:  mov r41 = r37
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r42 = r36
; CHECK-NEXT:  mov r43 = r35
; CHECK-NEXT:  mov r44 = r34
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r45 = r33
; CHECK-NEXT:  mov r46 = r32
; CHECK-NEXT:  .save rp, r40
; CHECK-NEXT:  mov r40 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
  call void @g(i64 %f, i64 %e, i64 %d, i64 %c, i64 %b, i64 %a)
  ret void
}

; Only port I0 shifts by a constant, and the I slots of a cycle take ports I0
; and I1 in order, so the shift goes before the zero extension.
define i64 @i0_only(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: i0_only:
; CHECK:       shl r15 = r33, 5
; CHECK-NEXT:  zxt4 r14 = r32 ;;
; OFF-LABEL:   i0_only:
; OFF:         zxt4 r14 = r32
; OFF-NEXT:    shl r15 = r33, 5 ;;
  %z = and i64 %a, 4294967295
  %s = shl i64 %b, 5
  %t = add i64 %c, 1
  %u = add i64 %z, %s
  %v = add i64 %u, %t
  ret i64 %v
}

; The assembler's MMI holds both loads, and nothing does better, so there is
; no directive. A template that leaves its last bundle emptier is no cheaper
; for it.
define i64 @loads(ptr %p, ptr %q, i64 %a) {
; CHECK-LABEL: loads:
; CHECK-NOT:   .m
; CHECK:       ld8 r14 = [r32]
; CHECK-NEXT:  ld8 r15 = [r33] ;;
; CHECK-NEXT:  add r14 = r14, r15 ;;
; CHECK-NEXT:  shladd r8 = r34, 3, r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = load i64, ptr %p
  %y = load i64, ptr %q
  %s = shl i64 %a, 3
  %t = add i64 %x, %y
  %u = add i64 %t, %s
  ret i64 %u
}

; Only port I0 moves to b0 and ar.pfs, so the two restores take two cycles in
; the group of the loads. The compare after the loads waits for them, and
; one restore goes to its group.
declare void @h()
define i64 @restores(ptr %p, ptr %q) {
; CHECK-LABEL: restores:
; CHECK:       br.call.sptk.many b0 = h ;;
; CHECK:       ld8 r14 = [r32]
; CHECK-NOT:   b0
; CHECK:       mov ar.pfs = r35 ;;
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  cmp.lt p6, p7 = r14, r15 ;;
  call void @h()
  %x = load i64, ptr %p
  %y = load i64, ptr %q
  %lt = icmp slt i64 %x, %y
  %r = zext i1 %lt to i64
  ret i64 %r
}
