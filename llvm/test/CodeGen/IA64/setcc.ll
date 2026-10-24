; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A compare writes a predicate and its complement, and the value of one is
; zero made one where the predicate says so. Only "equal" and "less than"
; are encoded: the rest exchange the operands or the predicates.

define i64 @eq(i64 %a, i64 %b) {
; CHECK-LABEL: eq:
; CHECK:       cmp.eq p6, p7 = r32, r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ne(i64 %a, i64 %b) {
; CHECK-LABEL: ne:
; CHECK:       cmp.eq p7, p6 = r32, r33
  %c = icmp ne i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @slt(i64 %a, i64 %b) {
; CHECK-LABEL: slt:
; CHECK:       cmp.lt p6, p7 = r32, r33
  %c = icmp slt i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sge(i64 %a, i64 %b) {
; CHECK-LABEL: sge:
; CHECK:       cmp.lt p7, p6 = r32, r33
  %c = icmp sge i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sgt(i64 %a, i64 %b) {
; CHECK-LABEL: sgt:
; CHECK:       cmp.lt p6, p7 = r33, r32
  %c = icmp sgt i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sle(i64 %a, i64 %b) {
; CHECK-LABEL: sle:
; CHECK:       cmp.lt p7, p6 = r33, r32
  %c = icmp sle i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ult(i64 %a, i64 %b) {
; CHECK-LABEL: ult:
; CHECK:       cmp.ltu p6, p7 = r32, r33
  %c = icmp ult i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @uge(i64 %a, i64 %b) {
; CHECK-LABEL: uge:
; CHECK:       cmp.ltu p7, p6 = r32, r33
  %c = icmp uge i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ugt(i64 %a, i64 %b) {
; CHECK-LABEL: ugt:
; CHECK:       cmp.ltu p6, p7 = r33, r32
  %c = icmp ugt i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ule(i64 %a, i64 %b) {
; CHECK-LABEL: ule:
; CHECK:       cmp.ltu p7, p6 = r33, r32
  %c = icmp ule i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; A constant of eight bits is the left operand of the compare, so "greater
; than" is direct and "less than" is the opposite of a compare against the
; constant below.

define i64 @eq_constant(i64 %a) {
; CHECK-LABEL: eq_constant:
; CHECK:       cmp.eq p6, p7 = 5, r32
  %c = icmp eq i64 %a, 5
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ne_zero(i64 %a) {
; CHECK-LABEL: ne_zero:
; CHECK:       cmp.eq p7, p6 = 0, r32
  %c = icmp ne i64 %a, 0
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sgt_constant(i64 %a) {
; CHECK-LABEL: sgt_constant:
; CHECK:       cmp.lt p6, p7 = 127, r32
  %c = icmp sgt i64 %a, 127
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @slt_constant(i64 %a) {
; CHECK-LABEL: slt_constant:
; CHECK:       cmp.lt p7, p6 = 127, r32
  %c = icmp slt i64 %a, 128
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @slt_smallest(i64 %a) {
; CHECK-LABEL: slt_smallest:
; CHECK:       cmp.lt p7, p6 = -128, r32
  %c = icmp slt i64 %a, -127
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ult_constant(i64 %a) {
; CHECK-LABEL: ult_constant:
; CHECK:       cmp.ltu p7, p6 = 9, r32
  %c = icmp ult i64 %a, 10
  %r = zext i1 %c to i64
  ret i64 %r
}

; The constant of an unsigned compare is sign-extended like any other, so
; the eight bits reach the top of the range.

define i64 @ugt_top(i64 %a) {
; CHECK-LABEL: ugt_top:
; CHECK:       cmp.ltu p6, p7 = -3, r32
  %c = icmp ugt i64 %a, -3
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sgt_too_large(i64 %a) {
; CHECK-LABEL: sgt_too_large:
; CHECK:       mov [[T:r[0-9]+]] = 128
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  cmp.lt p6, p7 = [[T]], r32
  %c = icmp sgt i64 %a, 128
  %r = zext i1 %c to i64
  ret i64 %r
}

; A narrow compare is of the low words, which need no extension.

define i64 @slt32(i32 %a, i32 %b) {
; CHECK-LABEL: slt32:
; CHECK-NOT:   sxt4
; CHECK:       cmp4.lt p6, p7 = r32, r33
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ult32(i32 %a, i32 %b) {
; CHECK-LABEL: ult32:
; CHECK-NOT:   addp4 {{.*}}, r0
; CHECK:       cmp4.ltu p6, p7 = r32, r33
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @eq32(i32 %a, i32 %b) {
; CHECK-LABEL: eq32:
; CHECK:       cmp4.eq p6, p7 = r32, r33
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; cmp4 sign-extends its immediate from eight bits, and compares the low 32
; bits of it.
define i64 @ult32_max(i32 %a) {
; CHECK-LABEL: ult32_max:
; CHECK:       cmp4.eq p7, p6 = -1, r32
  %c = icmp ult i32 %a, -1
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ugt32_big(i32 %a) {
; CHECK-LABEL: ugt32_big:
; CHECK:       movl [[C:r[0-9]+]] = 3000000000
; CHECK:       cmp4.ltu p6, p7 = [[C]], r32
  %c = icmp ugt i32 %a, 3000000000
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @sgt32_imm(i32 %a) {
; CHECK-LABEL: sgt32_imm:
; CHECK:       cmp4.lt p6, p7 = 7, r32
  %c = icmp sgt i32 %a, 7
  %r = zext i1 %c to i64
  ret i64 %r
}

; A 64-bit value, or values extended differently, need the full compare.
define i64 @wide32(i32 %a, i64 %b) {
; CHECK-LABEL: wide32:
; CHECK:       sxt4 [[A:r[0-9]+]] = r32 ;;
; CHECK-NEXT:  cmp.lt p6, p7 = [[A]], r33
  %e = sext i32 %a to i64
  %c = icmp slt i64 %e, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @mixed32(i32 %a, i32 %b) {
; CHECK-LABEL: mixed32:
; CHECK-DAG:   sxt4 [[A:r[0-9]+]] = r32
; CHECK-DAG:   addp4 [[B:r[0-9]+]] = r33, r0
; CHECK:       cmp.eq p6, p7 = [[A]], [[B]]
  %x = sext i32 %a to i64
  %y = zext i32 %b to i64
  %c = icmp eq i64 %x, %y
  %r = zext i1 %c to i64
  ret i64 %r
}

; Parallel compares of words accumulate into the same predicates in one
; instruction group. The sign test, which the combiner makes a test of the
; value shifted up to the top, is a test of the sign bit.
define i64 @or32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: or32:
; CHECK:       tbit.z p7, p6 = r34, 31
; CHECK-NOT:   shl
; CHECK:       cmp4.eq.or.andcm p6, p7 = 1, r32{{$}}
; CHECK-NEXT:  cmp4.eq.or.andcm p6, p7 = 2, r33 ;;
  %x = icmp eq i32 %a, 1
  %y = icmp eq i32 %b, 2
  %z = icmp slt i32 %c, 0
  %o = or i1 %x, %y
  %p = or i1 %o, %z
  %r = zext i1 %p to i64
  ret i64 %r
}

define i64 @and_sign16(i16 %c, i64 %d) {
; CHECK-LABEL: and_sign16:
; CHECK:       cmp.eq p6, p7 = 0, r33
; CHECK-NOT:   shl
; CHECK:       tbit.nz.or.andcm p7, p6 = r32, 15 ;;
  %z = icmp sgt i16 %c, -1
  %y = icmp eq i64 %d, 0
  %p = and i1 %y, %z
  %r = zext i1 %p to i64
  ret i64 %r
}

; Only a test of the sign is a test of one bit.
define i64 @shl_not_sign(i64 %a) {
; CHECK-LABEL: shl_not_sign:
; CHECK:       shl [[S:r[0-9]+]] = r32, 32 ;;
; CHECK-NEXT:  cmp.lt p7, p6 = 4, [[S]]
  %s = shl i64 %a, 32
  %c = icmp slt i64 %s, 5
  %r = zext i1 %c to i64
  ret i64 %r
}
