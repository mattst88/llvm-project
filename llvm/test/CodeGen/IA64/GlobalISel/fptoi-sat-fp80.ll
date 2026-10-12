; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 < %s | FileCheck %s

; The bounds that an extended value is held to are extended values, which
; hold every 64-bit integer exactly, and so the value is clamped and then
; converted.

; CHECK-LABEL: signed:
; CHECK:      movl r14 = @gprel(.LCPI0_1) ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14]
; CHECK-NEXT: movl r14 = @gprel(.LCPI0_0) ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f7 = [r14]
; CHECK-NEXT: fcmp.lt p6, p7 = f6, f8 ;;
; CHECK-NEXT: (p6) mov f6 = f8 ;;
; CHECK-NEXT: fcmp.lt p6, p7 = f6, f7 ;;
; CHECK-NEXT: (p6) mov f7 = f6
; CHECK-NEXT: fcmp.unord p6, p7 = f8, f8 ;;
; CHECK-NEXT: fcvt.fx.trunc f6 = f7 ;;
; CHECK-NEXT: getf.sig r8 = f6 ;;
; CHECK-NEXT: (p6) mov r8 = r0
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i64 @signed(x86_fp80 %a) {
  %r = call i64 @llvm.fptosi.sat.i64.f80(x86_fp80 %a)
  ret i64 %r
}

; CHECK-LABEL: unsigned:
; CHECK:      movl r14 = @gprel(.LCPI1_1) ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14]
; CHECK-NEXT: movl r14 = @gprel(.LCPI1_0) ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f7 = [r14]
; CHECK-NEXT: fcmp.lt p6, p7 = f6, f8 ;;
; CHECK-NEXT: (p6) mov f6 = f8 ;;
; CHECK-NEXT: fcmp.lt p6, p7 = f6, f7 ;;
; CHECK-NEXT: (p6) mov f7 = f6 ;;
; CHECK-NEXT: fcvt.fxu.trunc f6 = f7 ;;
; CHECK-NEXT: getf.sig r8 = f6
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i32 @unsigned(x86_fp80 %a) {
  %r = call i32 @llvm.fptoui.sat.i32.f80(x86_fp80 %a)
  ret i32 %r
}
