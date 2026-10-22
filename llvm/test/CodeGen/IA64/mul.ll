; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-pipeliner=false < %s | FileCheck %s

; The multiplier is in the floating-point unit and takes the significands
; of two of its registers.

define i64 @mul(i64 %a, i64 %b) {
; CHECK-LABEL: mul:
; CHECK:       setf.sig f6 = r33
; CHECK-NEXT:  setf.sig f7 = r32 ;;
; CHECK-NEXT:  xma.l f6 = f7, f6, f0 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, %b
  ret i64 %r
}

define i32 @mul32(i32 %a, i32 %b) {
; CHECK-LABEL: mul32:
; CHECK:       xma.l f6 = f7, f6, f0 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i32 %a, %b
  ret i32 %r
}

define i64 @high_unsigned(i64 %a, i64 %b) {
; CHECK-LABEL: high_unsigned:
; CHECK:       xma.hu f6 = f7, f6, f0 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %x = zext i64 %a to i128
  %y = zext i64 %b to i128
  %m = mul i128 %x, %y
  %s = lshr i128 %m, 64
  %r = trunc i128 %s to i64
  ret i64 %r
}

define i64 @high_signed(i64 %a, i64 %b) {
; CHECK-LABEL: high_signed:
; CHECK:       xma.h f6 = f7, f6, f0 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %x = sext i64 %a to i128
  %y = sext i64 %b to i128
  %m = mul i128 %x, %y
  %s = lshr i128 %m, 64
  %r = trunc i128 %s to i64
  ret i64 %r
}

; A product of two registers' worth is done in line.
define i128 @wide(i128 %a, i128 %b) {
; CHECK-LABEL: wide:
; CHECK-NOT:   br.call
; CHECK:       xma.hu
; CHECK:       br.ret.sptk.many b0 ;;
  %r = mul i128 %a, %b
  ret i128 %r
}

; The move of a factor to the floating-point unit does nothing but write its
; result, so the one of a factor that a loop does not change is done once.
define i64 @invariant(ptr %p, i64 %n, i64 %k) {
; CHECK-LABEL: invariant:
; CHECK:       setf.sig f6 = r34
; CHECK:       .LBB{{[0-9_]+}}:
; CHECK-NOT:   r34
; CHECK:       xma.l f7 = f7, f6, f0 ;;
; CHECK:       br.cloop.sptk.few .LBB
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %q
  %m = mul i64 %v, %k
  %s.next = add i64 %s, %m
  %i.next = add i64 %i, 1
  %c = icmp eq i64 %i.next, %n
  br i1 %c, label %exit, label %loop

exit:
  ret i64 %s.next
}

; A product with a constant is a chain of shifts, shladds and subtracts
; when that is shorter than the 14 cycles of setf.sig, xma.l and getf.sig.
define i64 @by3(i64 %a) {
; CHECK-LABEL: by3:
; CHECK:       shladd r8 = r32, 1, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 3
  ret i64 %r
}

define i64 @by10(i64 %a) {
; CHECK-LABEL: by10:
; CHECK:       shladd r14 = r32, 2, r32 ;;
; CHECK-NEXT:  shl r8 = r14, 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 10
  ret i64 %r
}

define i64 @by15(i64 %a) {
; CHECK-LABEL: by15:
; CHECK:       shl r14 = r32, 4 ;;
; CHECK-NEXT:  sub r8 = r14, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 15
  ret i64 %r
}

define i64 @by_minus3(i64 %a) {
; CHECK-LABEL: by_minus3:
; CHECK:       shladd r14 = r32, 1, r32 ;;
; CHECK-NEXT:  sub r8 = 0, r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, -3
  ret i64 %r
}

; 10000 is ((((9 * 2 + 1) * 2 + 1) * 16 + 1) * 16, five steps in a row, but
; also (5 << 7 - (16 - 1)) << 4, from its terms 2^13 + 2^11 - 2^8 + 2^4 as a
; tree, which takes four.
define i64 @by10000(i64 %a) {
; CHECK-LABEL: by10000:
; CHECK:       shl r14 = r32, 4
; CHECK-NEXT:  shladd r15 = r32, 2, r32 ;;
; CHECK-NEXT:  sub r14 = r32, r14
; CHECK-NEXT:  shl r15 = r15, 7 ;;
; CHECK-NEXT:  add r14 = r15, r14 ;;
; CHECK-NEXT:  shl r8 = r14, 4
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 10000
  ret i64 %r
}

; 1759218605, the multiplier that divides by 10000, takes eight operations in
; a row as a tree of halves, and six where each run of its terms is split at
; its shallowest sum.
define i64 @by1759218605(i64 %a) {
; CHECK-LABEL: by1759218605:
; CHECK:       shladd r15 = r32, 4, r32
; CHECK-NEXT:  shladd r16 = r32, 3, r32
; CHECK-NEXT:  shl r17 = r32, 5
; CHECK-NEXT:  shladd r14 = r32, 2, r32
; CHECK-NEXT:  shl r19 = r32, 10 ;;
; CHECK-NEXT:  shladd r14 = r15, 4, r14
; CHECK:       shladd r18 = r16, 3, r32
; CHECK-NEXT:  sub r16 = r16, r17
; CHECK-NEXT:  shl r20 = r32, 7 ;;
; CHECK-NEXT:  sub r14 = r19, r14
; CHECK-NEXT:  shl r21 = r18, 13
; CHECK-NEXT:  add r16 = r20, r16 ;;
; CHECK-NEXT:  sub r14 = r14, r21
; CHECK-NEXT:  shl r22 = r16, 22 ;;
; CHECK-NEXT:  add r14 = r22, r14 ;;
; CHECK-NEXT:  shladd r8 = r14, 2, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 1759218605
  ret i64 %r
}

; As a chain, without the tree.
; RUN: llc -mtriple=ia64-unknown-linux-gnu -ia64-pipeliner=false \
; RUN:   -ia64-mul-tree-max-ops=0 < %s | FileCheck %s --check-prefix=CHAIN
; CHAIN-LABEL: by10000:
; CHAIN:       shladd r14 = r32, 3, r32 ;;
; CHAIN-NEXT:  shladd r14 = r14, 1, r32 ;;
; CHAIN-NEXT:  shladd r14 = r14, 1, r32 ;;
; CHAIN-NEXT:  shladd r14 = r14, 4, r32 ;;
; CHAIN-NEXT:  shl r8 = r14, 4

define i32 @by3141(i32 %a) {
; CHECK-LABEL: by3141:
; CHECK:       shladd r14 = r32, 1, r32 ;;
; CHECK-NEXT:  shladd r14 = r14, 4, r32 ;;
; CHECK-NEXT:  shladd r14 = r14, 4, r32 ;;
; CHECK-NEXT:  shladd r8 = r14, 2, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i32 %a, 3141
  ret i32 %r
}

; A constant that takes more than 12 steps is multiplied.
define i64 @by_long(i64 %a) {
; CHECK-LABEL: by_long:
; CHECK:       movl r14 = 81985529216486895
; CHECK:       xma.l f6 = f7, f6, f0 ;;
  %r = mul i64 %a, 81985529216486895
  ret i64 %r
}

; As is any, when optimizing for size.
define i64 @by10_minsize(i64 %a) minsize {
; CHECK-LABEL: by10_minsize:
; CHECK:       mov r14 = 10
; CHECK:       xma.l f6 = f7, f6, f0 ;;
  %r = mul i64 %a, 10
  ret i64 %r
}

; A product of two values that fit in a halfword is pmpy2.r, which reads
; only the low halfword of each, so loaded values are not sign-extended first.
define i32 @halfwords(ptr %a, ptr %b) {
; CHECK-LABEL: halfwords:
; CHECK:       ld2 r14 = [r32]
; CHECK-NEXT:  ld2 r15 = [r33] ;;
; CHECK-NEXT:  pmpy2.r r14 = r14, r15 ;;
; CHECK-NEXT:  sxt4 r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = load i16, ptr %a
  %y = load i16, ptr %b
  %xs = sext i16 %x to i32
  %ys = sext i16 %y to i32
  %r = mul i32 %xs, %ys
  ret i32 %r
}

; Its low word is the whole product.
define i64 @halfwords_wide(i16 %a, i16 %b) {
; CHECK-LABEL: halfwords_wide:
; CHECK:       pmpy2.r r14 = r32, r33 ;;
; CHECK-NEXT:  sxt4 r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = sext i16 %a to i64
  %y = sext i16 %b to i64
  %r = mul i64 %x, %y
  ret i64 %r
}

; Nothing is extended where only the low word is used.
define void @halfwords_stored(ptr %a, ptr %b, ptr %c, i32 %s) {
; CHECK-LABEL: halfwords_stored:
; CHECK:       pmpy2.r r14 = r14, r15 ;;
; CHECK-NEXT:  add r14 = r14, r35 ;;
; CHECK-NEXT:  st4 [r34] = r14
  %x = load i16, ptr %a
  %y = load i16, ptr %b
  %xs = sext i16 %x to i32
  %ys = sext i16 %y to i32
  %m = mul i32 %xs, %ys
  %r = add i32 %m, %s
  store i32 %r, ptr %c
  ret void
}

define i32 @fields(i32 %a) {
; CHECK-LABEL: fields:
; CHECK:       extr.u r14 = r32, 2, 4
; CHECK-NEXT:  extr.u r15 = r32, 5, 7 ;;
; CHECK-NEXT:  pmpy2.r r14 = r14, r15 ;;
  %x = lshr i32 %a, 2
  %xf = and i32 %x, 15
  %y = lshr i32 %a, 5
  %yf = and i32 %y, 127
  %r = mul i32 %xf, %yf
  ret i32 %r
}

; An unsigned halfword does not fit in a signed one.
define i64 @unsigned_halfwords(i16 %a, i16 %b) {
; CHECK-LABEL: unsigned_halfwords:
; CHECK-NOT:   pmpy2.r
; CHECK:       xma.l f6 = f7, f6, f0 ;;
  %x = zext i16 %a to i64
  %y = zext i16 %b to i64
  %r = mul i64 %x, %y
  ret i64 %r
}
