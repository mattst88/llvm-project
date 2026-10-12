; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; What an extended value is cannot be read from its bits as it can from those
; of a double. A class that one compare tells from every other is told by the
; compare: unordered with itself for a NaN, equal to infinity or to zero for
; those, and the magnitude compared where either sign will do.

; CHECK-LABEL: nan:
; CHECK:      fcmp.unord p6, p7 = f8, f8
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @nan(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 3)
  ret i1 %c
}

; CHECK-LABEL: notnan:
; CHECK:      fcmp.unord p7, p6 = f8, f8
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @notnan(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 1020)
  ret i1 %c
}

; CHECK-LABEL: inf:
; CHECK:      movl r14 = @gprel(.LCPI2_0)
; CHECK-NEXT: fmerge.s f7 = f0, f8
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14] ;;
; CHECK-NEXT: fcmp.eq p6, p7 = f7, f6 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @inf(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 516)
  ret i1 %c
}

; CHECK-LABEL: finite:
; CHECK:      movl r14 = @gprel(.LCPI3_0)
; CHECK-NEXT: fmerge.s f7 = f0, f8
; CHECK-NEXT: mov r15 = r0 ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14]
; CHECK-NEXT: mov r14 = r0 ;;
; CHECK-NEXT: fcmp.lt p6, p7 = f7, f6 ;;
; CHECK-NEXT: (p6) mov r14 = 1
; CHECK-NEXT: fcmp.lt p6, p7 = f6, f7 ;;
; CHECK-NEXT: (p6) mov r15 = 1 ;;
; CHECK-NEXT: or r8 = r14, r15
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @finite(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 504)
  ret i1 %c
}

; CHECK-LABEL: neginf:
; CHECK:      movl r14 = @gprel(.LCPI4_0)
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14] ;;
; CHECK-NEXT: fcmp.eq p6, p7 = f8, f6 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @neginf(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 4)
  ret i1 %c
}

; CHECK-LABEL: zero:
; CHECK:      movl r14 = @gprel(.LCPI5_0)
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: add r14 = r14, r1 ;;
; CHECK-NEXT: ldfe f6 = [r14] ;;
; CHECK-NEXT: fcmp.eq p6, p7 = f8, f6 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @zero(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 96)
  ret i1 %c
}
