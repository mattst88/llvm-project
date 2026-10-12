; RUN: split-file %s %t
; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %t/stackmap.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %t/patchpoint.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %t/deopt.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %t/stackmap.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %t/deopt.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s

; No stack map section is written and no room is left at a patch point, so
; what asks for either is an error and not an abort further on.

; CHECK: LLVM ERROR: stack maps, patch points and statepoints are not supported on IA-64

;--- stackmap.ll
declare void @llvm.experimental.stackmap(i64, i32, ...)

define void @f(i64 %a) {
  %x = alloca i64
  call void (i64, i32, ...) @llvm.experimental.stackmap(i64 1, i32 0, i64 %a, ptr %x)
  ret void
}

;--- patchpoint.ll
declare void @llvm.experimental.patchpoint.void(i64, i32, ptr, i32, ...)
declare void @g(i64)

define void @f(i64 %a) {
  call void (i64, i32, ptr, i32, ...) @llvm.experimental.patchpoint.void(i64 1, i32 32, ptr @g, i32 1, i64 %a)
  ret void
}

;--- deopt.ll
declare void @g(i64)

define void @f(i64 %a) {
  call void @g(i64 1) [ "deopt"(i64 %a) ]
  ret void
}
