; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

declare ptr @llvm.returnaddress(i32)
declare ptr @llvm.frameaddress.p0(i32)
declare ptr @llvm.thread.pointer.p0()
declare void @llvm.prefetch.p0(ptr, i32, i32, i32)
declare void @llvm.trap()
declare void @llvm.debugtrap()
declare void @g()

; The function is entered with its return address in b0, and a call puts
; another there.
define ptr @ra() {
; CHECK-LABEL: ra:
; CHECK:       mov r8 = b0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

define ptr @ra_call() {
; CHECK-LABEL: ra_call:
; CHECK:       mov r32 = b0
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK:       mov r8 = r32
  call void @g()
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

; Whoever called the caller is not looked for.
define ptr @ra1() {
; CHECK-LABEL: ra1:
; CHECK:       mov r8 = r0
  %r = call ptr @llvm.returnaddress(i32 1)
  ret ptr %r
}

; The address of a frame is the stack pointer its function was entered with.
define ptr @fa() {
; CHECK-LABEL: fa:
; CHECK:       mov r33 = r12 ;;
; CHECK:       mov r8 = r33
  %r = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %r
}

define ptr @fa1() {
; CHECK-LABEL: fa1:
; CHECK:       mov r8 = r0
  %r = call ptr @llvm.frameaddress.p0(i32 1)
  ret ptr %r
}

define ptr @tp() {
; CHECK-LABEL: tp:
; CHECK:       mov r8 = r13
  %r = call ptr @llvm.thread.pointer.p0()
  ret ptr %r
}

define void @pf(ptr %p) {
; CHECK-LABEL: pf:
; CHECK:       lfetch [r32]
; CHECK-NEXT:  lfetch.excl.nta [r32]
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 3, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 0, i32 1)
  ret void
}

define void @trap() {
; CHECK-LABEL: trap:
; CHECK:       break.m 0 ;;
  call void @llvm.trap()
  unreachable
}

define void @dtrap() {
; CHECK-LABEL: dtrap:
; CHECK:       break.m 838860
  call void @llvm.debugtrap()
  ret void
}
