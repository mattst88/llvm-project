; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; The functions that round to an integer return a long, and a result that is
; asked for as an int is the low half of it.

define i32 @round(double %x) {
; CHECK-LABEL: round:
; CHECK:       br.call.sptk.many b0 = lround ;;
; CHECK-NOT:   r8 =
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.lround.i32.f64(double %x)
  ret i32 %r
}

define i32 @rint(float %x) {
; CHECK-LABEL: rint:
; CHECK:       br.call.sptk.many b0 = lrintf ;;
; CHECK-NOT:   r8 =
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.lrint.i32.f32(float %x)
  ret i32 %r
}

define i32 @extended(x86_fp80 %x) {
; CHECK-LABEL: extended:
; CHECK:       br.call.sptk.many b0 = lroundl ;;
; CHECK-NOT:   r8 =
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.lround.i32.f80(x86_fp80 %x)
  ret i32 %r
}
