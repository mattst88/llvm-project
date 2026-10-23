; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The next index is loaded from tt[t], where t is a phi of the block that
; the CRC update ends in, as in bzip2's output loop. Issued at the top of
; that block, the load would wait for the CRC table load. Each predecessor
; issues it instead, from the value t takes there, and the check takes the
; same register on both paths.
; CHECK-LABEL: join:
; CHECK:       // %crcblk
; CHECK:       adds [[T:r[0-9]+]] = 1, r35
; CHECK:       shladd [[A:r[0-9]+]] = [[T]], 3, r32
; CHECK:       ld8.sa [[W:r[0-9]+]] = {{\[}}[[A]]]
; CHECK:       st1
; CHECK:       br.cond
; CHECK:       // %next
; CHECK:       ld8.c.clr [[W]] = {{\[}}[[A]]]
; CHECK:       shladd [[A]] = r35, 3, r32
; CHECK:       ld8.sa [[W]] = {{\[}}[[A]]]
define i32 @join(ptr %tt, ptr %crc, ptr %out, i64 %t0, i32 %c0, i32 %n, i64 %m) {
entry:
  br label %loop

loop:
  %t = phi i64 [ %t0, %entry ], [ %tn, %next ]
  %c = phi i32 [ %c0, %entry ], [ %c2, %next ]
  %i = phi i32 [ 0, %entry ], [ %i1, %next ]
  %stop = icmp eq i64 %t, %m
  br i1 %stop, label %exit, label %body, !prof !1

body:
  %b = and i64 %t, 256
  %odd = icmp ne i64 %b, 0
  br i1 %odd, label %crcblk, label %join, !prof !0

crcblk:
  %lo = trunc i64 %t to i32
  %lo8 = and i32 %lo, 255
  %lc = lshr i32 %c, 24
  %x = xor i32 %lo8, %lc
  %idx = zext i32 %x to i64
  %p = getelementptr inbounds i32, ptr %crc, i64 %idx
  %v = load i32, ptr %p, align 4
  %cs = shl i32 %c, 8
  %c1 = xor i32 %v, %cs
  %ob = trunc i64 %t to i8
  store i8 %ob, ptr %out, align 1
  %tx = add i64 %t, 1
  br label %join

join:
  %c2 = phi i32 [ %c, %body ], [ %c1, %crcblk ]
  %tj = phi i64 [ %t, %body ], [ %tx, %crcblk ]
  %i2 = shl i32 %i, 3
  %i3 = xor i32 %i2, %c2
  %i4 = lshr i32 %i3, 5
  %i1 = add i32 %i4, %c2
  %done = icmp eq i32 %i1, %n
  br i1 %done, label %exit, label %next, !prof !1

next:
  %q = getelementptr inbounds i64, ptr %tt, i64 %tj
  %w = load i64, ptr %q, align 8
  %tn = lshr i64 %w, 9
  br label %loop

exit:
  %r = phi i32 [ %c, %loop ], [ %c2, %join ]
  ret i32 %r
}

; When the hot predecessor computes little, the load cannot move up in it,
; and issuing it in each predecessor would only add a load and phis. It is
; issued once, in the join.
; CHECK-LABEL: join_short:
; CHECK:       (p7) adds r35 = 1, r35
; CHECK-NOT:   ld8
; CHECK:       ld8.sa [[W:r[0-9]+]] = {{\[}}[[A:r[0-9]+]]]
; CHECK-NOT:   ld8
; CHECK:       ld8.c.clr [[W]] = {{\[}}[[A]]]
define i32 @join_short(ptr %tt, ptr %crc, ptr %out, i64 %t0, i32 %c0, i32 %n, i64 %m) {
entry:
  br label %loop

loop:
  %t = phi i64 [ %t0, %entry ], [ %tn, %next ]
  %c = phi i32 [ %c0, %entry ], [ %c2, %next ]
  %i = phi i32 [ 0, %entry ], [ %i1, %next ]
  %stop = icmp eq i64 %t, %m
  br i1 %stop, label %exit, label %body, !prof !1

body:
  %b = and i64 %t, 256
  %odd = icmp ne i64 %b, 0
  br i1 %odd, label %crcblk, label %join, !prof !0

crcblk:
  %c1 = add i32 %c, 7
  %tx = add i64 %t, 1
  br label %join

join:
  %c2 = phi i32 [ %c, %body ], [ %c1, %crcblk ]
  %tj = phi i64 [ %t, %body ], [ %tx, %crcblk ]
  %i2 = shl i32 %i, 3
  %i3 = xor i32 %i2, %c2
  %i4 = lshr i32 %i3, 5
  %i1 = add i32 %i4, %c2
  %done = icmp eq i32 %i1, %n
  br i1 %done, label %exit, label %next, !prof !1

next:
  %q = getelementptr inbounds i64, ptr %tt, i64 %tj
  %w = load i64, ptr %q, align 8
  %tn = lshr i64 %w, 9
  br label %loop

exit:
  %r = phi i32 [ %c, %loop ], [ %c2, %join ]
  ret i32 %r
}

!0 = !{!"branch_weights", i32 1000, i32 1}
!1 = !{!"branch_weights", i32 1, i32 1000}
