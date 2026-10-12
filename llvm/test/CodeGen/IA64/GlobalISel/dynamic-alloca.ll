; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

declare void @g(ptr, i64)

; The stack pointer moves down by the size, and what was allocated starts
; sixteen bytes above it, past the scratch area of the callee.
define void @f(i64 %n) {
; CHECK-LABEL: f:
; CHECK:       mov r35 = r12
; CHECK:       and r14 = -16, r14 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r36 = 16, r12
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK:       mov r12 = r35
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  ret void
}

; An alignment greater than the stack's is had by allocating more and
; rounding the address up.
define void @aligned(i64 %n) {
; CHECK-LABEL: aligned:
; CHECK:       and r14 = -16, r14 ;;
; CHECK-NEXT:  adds r14 = 48, r14 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  adds r14 = 63, r14 ;;
; CHECK-NEXT:  and r37 = -64, r14
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
  %a = alloca i8, i64 %n, align 64
  call void @g(ptr %a, i64 %n)
  ret void
}

declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)

; The stack pointer that is saved is put back.
define void @saved(i64 %n) {
; CHECK-LABEL: saved:
; CHECK:       mov r34 = r12
; CHECK:       sub r12 = r12, r14 ;;
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r12 = r34
; CHECK:       br.call.sptk.many b0 = g ;;
  %s = call ptr @llvm.stacksave()
  %a = alloca i64, i64 %n
  call void @g(ptr %a, i64 %n)
  call void @llvm.stackrestore(ptr %s)
  call void @g(ptr null, i64 %n)
  ret void
}
