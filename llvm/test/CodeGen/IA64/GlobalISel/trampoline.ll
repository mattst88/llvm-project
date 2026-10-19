; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A trampoline is four words: a descriptor whose entry is the code of
; __ia64_trampoline and whose global pointer is the address of the third
; word, and then the address of the function's descriptor and its static
; chain. It is called by the address it has.

declare void @llvm.init.trampoline(ptr, ptr, ptr)
declare ptr @llvm.adjust.trampoline(ptr)
declare i64 @nested(ptr nest, i64)

define ptr @make(ptr %tramp, ptr %chain) {
; CHECK-LABEL: make:
; CHECK-DAG:   addl [[E:r[0-9]+]] = @ltoff(__ia64_trampoline), r1
; CHECK-DAG:   addl [[F:r[0-9]+]] = @ltoff(@fptr(nested)), r1
; CHECK-DAG:   mov r8 = r32
; CHECK-DAG:   ld8 [[ENTRY:r[0-9]+]] = {{\[}}[[E]]{{\]}}
; CHECK-DAG:   ld8 [[FUNC:r[0-9]+]] = {{\[}}[[F]]{{\]}}
; CHECK-DAG:   adds [[THIRD:r[0-9]+]] = 16, r32
; CHECK-DAG:   st8 [r32] = [[ENTRY]]
; CHECK-DAG:   adds [[SECOND:r[0-9]+]] = 8, r32
; CHECK-DAG:   st8 {{\[}}[[SECOND]]{{\]}} = [[THIRD]]
; CHECK-DAG:   st8 {{\[}}[[THIRD]]{{\]}} = [[FUNC]]
; CHECK-DAG:   adds [[FOURTH:r[0-9]+]] = 24, r32
; CHECK:       st8 {{\[}}[[FOURTH]]{{\]}} = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  call void @llvm.init.trampoline(ptr %tramp, ptr @nested, ptr %chain)
  %p = call ptr @llvm.adjust.trampoline(ptr %tramp)
  ret ptr %p
}
