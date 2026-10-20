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

; 10000 is ((((9 * 2 + 1) * 2 + 1) * 16 + 1) * 16, five steps.
define i64 @by10000(i64 %a) {
; CHECK-LABEL: by10000:
; CHECK:       shladd r14 = r32, 3, r32 ;;
; CHECK-NEXT:  shladd r14 = r14, 1, r32 ;;
; CHECK-NEXT:  shladd r14 = r14, 1, r32 ;;
; CHECK-NEXT:  shladd r14 = r14, 4, r32 ;;
; CHECK-NEXT:  shl r8 = r14, 4
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = mul i64 %a, 10000
  ret i64 %r
}

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
