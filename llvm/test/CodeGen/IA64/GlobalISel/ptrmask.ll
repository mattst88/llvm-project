; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

declare ptr @llvm.ptrmask.p0.i64(ptr, i64)

; A pointer is masked as the integer it is.
define ptr @variable(ptr %p, i64 %m) {
; CHECK-LABEL: variable:
; CHECK:       and r8 = r32, r33
  %r = call ptr @llvm.ptrmask.p0.i64(ptr %p, i64 %m)
  ret ptr %r
}

define ptr @constant(ptr %p) {
; CHECK-LABEL: constant:
; CHECK:       and r8 = -16, r32
  %r = call ptr @llvm.ptrmask.p0.i64(ptr %p, i64 -16)
  ret ptr %r
}
