; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 < %s | FileCheck %s

; A vector is converted an element at a time.

; CHECK-LABEL: each:
; CHECK:      fcvt.fx.trunc f6 = f8
; CHECK-NEXT: movl r14 = 3472883712
; CHECK-NEXT: fcvt.fx.trunc f7 = f9 ;;
; CHECK-NEXT: getf.sig r8 = f6
; CHECK-NEXT: setf.s f6 = r14
; CHECK-NEXT: getf.sig r9 = f7
; CHECK-NEXT: movl r14 = 1325400063 ;;
; CHECK-NEXT: setf.s f7 = r14
; CHECK-NEXT: movl r14 = -2147483648
; CHECK-NEXT: fcmp.le p7, p6 = f6, f8 ;;
; CHECK-NEXT: (p6) mov r8 = r14
; CHECK-NEXT: fcmp.le p7, p6 = f6, f9 ;;
; CHECK-NEXT: (p6) mov r9 = r14
; CHECK-NEXT: fcmp.lt p6, p7 = f7, f8
; CHECK-NEXT: movl r14 = 2147483647 ;;
; CHECK-NEXT: (p6) mov r8 = r14
; CHECK-NEXT: fcmp.lt p6, p7 = f7, f9 ;;
; CHECK-NEXT: (p6) mov r9 = r14
; CHECK-NEXT: fcmp.unord p6, p7 = f8, f8 ;;
; CHECK-NEXT: (p6) mov r8 = r0
; CHECK-NEXT: fcmp.unord p6, p7 = f9, f9 ;;
; CHECK-NEXT: (p6) mov r9 = r0
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define <2 x i32> @each(float %x, float %y) {
  %v = insertelement <2 x float> poison, float %x, i32 0
  %a = insertelement <2 x float> %v, float %y, i32 1
  %r = call <2 x i32> @llvm.fptosi.sat.v2i32.v2f32(<2 x float> %a)
  ret <2 x i32> %r
}
