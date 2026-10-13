; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; What an extended value is cannot be read from its bits as it can from those
; of a double. fclass says it, and unlike a compare raises nothing when the
; value is a signaling NaN. One test tells a set of classes that is the same
; for both signs; a set that is not takes a test for each sign.

; CHECK-LABEL: nan:
; CHECK:      fclass.m p6, p7 = f8, 192
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @nan(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 3)
  ret i1 %c
}

; CHECK-LABEL: notnan:
; CHECK:      fclass.m p6, p7 = f8, 63
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @notnan(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 1020)
  ret i1 %c
}

; CHECK-LABEL: inf:
; CHECK:      fclass.m p6, p7 = f8, 35
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @inf(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 516)
  ret i1 %c
}

; CHECK-LABEL: finite:
; CHECK:      fclass.m p6, p7 = f8, 31
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @finite(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 504)
  ret i1 %c
}

; CHECK-LABEL: neginf:
; CHECK:      fclass.m p6, p7 = f8, 34
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @neginf(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 4)
  ret i1 %c
}

; CHECK-LABEL: zero:
; CHECK:      fclass.m p6, p7 = f8, 7
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @zero(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 96)
  ret i1 %c
}

; CHECK-LABEL: normal:
; CHECK:      fclass.m p6, p7 = f8, 19
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @normal(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 264)
  ret i1 %c
}

; CHECK-LABEL: snan:
; CHECK:      fclass.m p6, p7 = f8, 64
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @snan(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 1)
  ret i1 %c
}

; A positive normal number or a negative zero.
; CHECK-LABEL: two:
; CHECK:      fclass.m p6, p7 = f8, 17
; CHECK-NEXT: mov r14 = r0
; CHECK-NEXT: mov r15 = r0 ;;
; CHECK-NEXT: (p6) mov r14 = 1
; CHECK-NEXT: fclass.m p6, p7 = f8, 6 ;;
; CHECK-NEXT: (p6) mov r15 = 1 ;;
; CHECK-NEXT: or r8 = r14, r15
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @two(x86_fp80 %x) {
  %c = call i1 @llvm.is.fpclass.f80(x86_fp80 %x, i32 288)
  ret i1 %c
}

declare i1 @llvm.is.fpclass.f80(x86_fp80, i32)
