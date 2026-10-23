; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A rotate or funnel shift by a constant is shrp, the low 64 bits of the
; concatenation of two registers shifted right.

define i64 @rotl64(i64 %x) {
; CHECK-LABEL: rotl64:
; CHECK:       shrp r8 = r32, r32, 51
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 13)
  ret i64 %r
}

define i64 @rotr64(i64 %x) {
; CHECK-LABEL: rotr64:
; CHECK:       shrp r8 = r32, r32, 13
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 13)
  ret i64 %r
}

define i64 @rotl64_shifts(i64 %x) {
; CHECK-LABEL: rotl64_shifts:
; CHECK:       shrp r8 = r32, r32, 63
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %l = shl i64 %x, 1
  %h = lshr i64 %x, 63
  %r = or i64 %l, %h
  ret i64 %r
}

define i64 @fshl64(i64 %x, i64 %y) {
; CHECK-LABEL: fshl64:
; CHECK:       shrp r8 = r32, r33, 54
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.fshl.i64(i64 %x, i64 %y, i64 10)
  ret i64 %r
}

define i64 @fshr64(i64 %x, i64 %y) {
; CHECK-LABEL: fshr64:
; CHECK:       shrp r8 = r32, r33, 10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.fshr.i64(i64 %x, i64 %y, i64 10)
  ret i64 %r
}

; A 32-bit one shifts the low operand into the upper half first.

define i32 @rotl32(i32 %x) {
; CHECK-LABEL: rotl32:
; CHECK:       shl r14 = r32, 32 ;;
; CHECK-NEXT:  shrp r8 = r32, r14, 59
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 5)
  ret i32 %r
}

define i32 @rotr32(i32 %x) {
; CHECK-LABEL: rotr32:
; CHECK:       shl r14 = r32, 32 ;;
; CHECK-NEXT:  shrp r8 = r32, r14, 34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 2)
  ret i32 %r
}

define i32 @fshl32(i32 %x, i32 %y) {
; CHECK-LABEL: fshl32:
; CHECK:       shl r14 = r33, 32 ;;
; CHECK-NEXT:  shrp r8 = r32, r14, 54
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %y, i32 10)
  ret i32 %r
}

; By a variable a 64-bit rotate is two shifts, and a 32-bit one shifts the
; value concatenated with itself.

define i64 @rotl64_variable(i64 %x, i64 %n) {
; CHECK-LABEL: rotl64_variable:
; CHECK-DAG:   shl r{{[0-9]+}} = r32, r{{[0-9]+}}
; CHECK-DAG:   shr.u r{{[0-9]+}} = r32, r{{[0-9]+}}
; CHECK:       or r8 =
  %r = call i64 @llvm.fshl.i64(i64 %x, i64 %x, i64 %n)
  ret i64 %r
}

define i32 @rotl32_variable(i32 %x, i32 %n) {
; CHECK-LABEL: rotl32_variable:
; CHECK-DAG:   zxt4 [[LO:r[0-9]+]] = r32
; CHECK-DAG:   shl [[HI:r[0-9]+]] = r32, 32
; CHECK-DAG:   and [[N:r[0-9]+]] = 31, r33
; CHECK:       or [[BOTH:r[0-9]+]] = [[HI]], [[LO]] ;;
; CHECK-NEXT:  shl [[R:r[0-9]+]] = [[BOTH]], [[N]] ;;
; CHECK-NEXT:  shr.u r8 = [[R]], 32
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}

define i32 @rotr32_variable(i32 %x, i32 %n) {
; CHECK-LABEL: rotr32_variable:
; CHECK-DAG:   zxt4 [[LO:r[0-9]+]] = r32
; CHECK-DAG:   shl [[HI:r[0-9]+]] = r32, 32
; CHECK-DAG:   and [[N:r[0-9]+]] = 31, r33
; CHECK:       or [[BOTH:r[0-9]+]] = [[HI]], [[LO]] ;;
; CHECK-NEXT:  shr.u r8 = [[BOTH]], [[N]]
  %r = call i32 @llvm.fshr.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}
