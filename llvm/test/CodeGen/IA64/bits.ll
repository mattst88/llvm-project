; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

define i64 @popcount(i64 %a) {
; CHECK-LABEL: popcount:
; CHECK:       popcnt r8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.ctpop.i64(i64 %a)
  ret i64 %r
}

; The bits below the lowest one that is set, counted. There are sixty-four
; of them when none is set, so nothing has to ask.
define i64 @trailing(i64 %a) {
; CHECK-LABEL: trailing:
; CHECK:       adds r14 = -1, r32 ;;
; CHECK-NEXT:  andcm r14 = r14, r32 ;;
; CHECK-NEXT:  popcnt r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.cttz.i64(i64 %a, i1 false)
  ret i64 %r
}

; The highest bit that is set is copied into every one below it, and the
; ones that are left are counted.
define i64 @leading(i64 %a) {
; CHECK-LABEL: leading:
; CHECK:       shr.u r14 = r32, 1 ;;
; CHECK-NEXT:  or r14 = r32, r14 ;;
; CHECK:       shr.u r15 = r14, 32 ;;
; CHECK-NEXT:  or r14 = r14, r15 ;;
; CHECK-NEXT:  xor r14 = -1, r14 ;;
; CHECK-NEXT:  popcnt r8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.ctlz.i64(i64 %a, i1 false)
  ret i64 %r
}

define i64 @swap(i64 %a) {
; CHECK-LABEL: swap:
; CHECK:       mux1 r8 = r32, @rev
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.bswap.i64(i64 %a)
  ret i64 %r
}

define i32 @swap32(i32 %a) {
; CHECK-LABEL: swap32:
; CHECK:       mux1 r14 = r32, @rev ;;
; CHECK-NEXT:  shr.u r8 = r14, 32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.bswap.i32(i32 %a)
  ret i32 %r
}

define i64 @rotate(i64 %a, i64 %b) {
; CHECK-LABEL: rotate:
; CHECK:       and r14 = 63, r33 ;;
; CHECK-NEXT:  shl r14 = r32, r14
; CHECK-NEXT:  sub r15 = 0, r33 ;;
; CHECK-NEXT:  and r15 = 63, r15 ;;
; CHECK-NEXT:  shr.u r15 = r32, r15 ;;
; CHECK-NEXT:  or r8 = r14, r15
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @llvm.fshl.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}

; A shift of two registers is done in line.
define i128 @wide(i128 %a, i128 %b) {
; CHECK-LABEL: wide:
; CHECK-NOT:   br.call
; CHECK:       shl r8 = r32, r15
; CHECK:       (p6) mov r9 = r8
; CHECK:       (p6) mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = shl i128 %a, %b
  ret i128 %r
}
