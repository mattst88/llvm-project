; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The thread pointer is r13, which nothing else is ever kept in.

declare ptr @llvm.thread.pointer.p0()

define ptr @tp() {
; CHECK-LABEL: tp:
; CHECK:       mov r8 = r13
; CHECK-NEXT:  br.ret.sptk.many b0
  %p = call ptr @llvm.thread.pointer.p0()
  ret ptr %p
}

define i64 @below() {
; CHECK-LABEL: below:
; CHECK:       adds r14 = -16, r13 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
  %p = call ptr @llvm.thread.pointer.p0()
  %q = getelementptr i8, ptr %p, i64 -16
  %v = load i64, ptr %q
  ret i64 %v
}
