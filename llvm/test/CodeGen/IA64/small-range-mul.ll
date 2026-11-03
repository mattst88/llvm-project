; A multiply by a factor of few values, like CPython's sign of an integer, is
; ALU work rather than a trip through the FP unit.
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 \
; RUN:   -ia64-expand-small-range-mul=false < %s | FileCheck %s --check-prefix=OFF

; (1 - (tag & 3)) * x = x - ((x & -bit0) + ((x & -bit1) << 1))
; CHECK-LABEL: sign:
; CHECK-NOT:     xma
; CHECK-DAG:     extr [[M0:r[0-9]+]] = r32, 0, 1
; CHECK-DAG:     extr [[M1:r[0-9]+]] = r32, 1, 1
; CHECK-DAG:     and [[A0:r[0-9]+]] = r33, [[M0]]
; CHECK-DAG:     and [[A1:r[0-9]+]] = r33, [[M1]]
; CHECK:         shladd [[S:r[0-9]+]] = [[A1]], 1, [[A0]]
; CHECK:         sub r8 = r33, [[S]]
; CHECK-NOT:     xma
; CHECK:         .size sign
; OFF-LABEL:   sign:
; OFF:           xma.l
define i64 @sign(i64 %tag, i64 %x) {
  %and = and i64 %tag, 3
  %s = sub nsw i64 1, %and
  %m = mul nsw i64 %s, %x
  ret i64 %m
}

; (y + 1) * x with y one bit: x + (x & -y)
; CHECK-LABEL: onebit:
; CHECK-NOT:     xma
; CHECK:         extr {{r[0-9]+}} = r32, 4, 1
; CHECK-NOT:     xma
; CHECK:         .size onebit
define i64 @onebit(i64 %t, i64 %x) {
  %b = and i64 %t, 16
  %y = lshr i64 %b, 4
  %s = add i64 %y, 1
  %m = mul i64 %x, %s
  ret i64 %m
}

; Three unknown bits, CPython's string kind.
; CHECK-LABEL: threebits:
; CHECK-NOT:     xma
; CHECK:         shladd
; CHECK-NOT:     xma
; CHECK:         .size threebits
define i32 @threebits(i32 %tag, i32 %x) {
  %and = and i32 %tag, 7
  %m = mul i32 %and, %x
  ret i32 %m
}

; Four are too many.
; CHECK-LABEL: fourbits:
; CHECK:         xma.l
define i64 @fourbits(i64 %tag, i64 %x) {
  %and = and i64 %tag, 15
  %m = mul i64 %and, %x
  ret i64 %m
}

; So is an offset too large to multiply by cheaply.
; CHECK-LABEL: bigoffset:
; CHECK:         xma.l
define i64 @bigoffset(i64 %tag, i64 %x) {
  %and = and i64 %tag, 1
  %s = add i64 %and, 1000
  %m = mul i64 %s, %x
  ret i64 %m
}

; A choice of powers of two, CPython's character size, is a shift.
; CHECK-LABEL: pow2phi:
; CHECK-NOT:     xma
; CHECK:         shl
; CHECK-NOT:     xma
; CHECK:         .size pow2phi
define i64 @pow2phi(i1 %c, i1 %d, i64 %x) {
entry:
  br i1 %c, label %a, label %b
a:
  br i1 %d, label %join, label %b
b:
  br label %join
join:
  %s = phi i64 [ 2, %a ], [ 4, %b ]
  %m = mul i64 %x, %s
  ret i64 %m
}

