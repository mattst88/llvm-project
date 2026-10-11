; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; An unwinder flushes the register stack and then asks where the registers of
; its own frame would go, and what is below that is its callers'.

declare ptr @llvm.ia64.bsp()
declare void @llvm.ia64.flushrs()

define ptr @bsp() {
; CHECK-LABEL: bsp:
; CHECK:       mov r8 = ar.bsp
; CHECK:       br.ret.sptk.many b0
  %p = call ptr @llvm.ia64.bsp()
  ret ptr %p
}

; The flush is the first instruction of its group, and the read after it is
; in another.
define ptr @flush(i64 %a) {
; CHECK-LABEL: flush:
; CHECK:       flushrs ;;
; CHECK-NEXT:  mov r8 = ar.bsp
  call void @llvm.ia64.flushrs()
  %p = call ptr @llvm.ia64.bsp()
  ret ptr %p
}

; An alloc moves the frame, so the read is not in its group.
declare void @g()
define ptr @frame() {
; CHECK-LABEL: frame:
; CHECK:       alloc
; CHECK:       ;;
; CHECK-NEXT:  mov {{r[0-9]+}} = ar.bsp
; CHECK:       br.call.sptk.many b0 = g
  %p = call ptr @llvm.ia64.bsp()
  call void @g()
  ret ptr %p
}

; Each one asks again.
define i1 @twice() {
; CHECK-LABEL: twice:
; CHECK:       mov {{r[0-9]+}} = ar.bsp ;;
; CHECK-NEXT:  flushrs ;;
; CHECK-NEXT:  mov {{r[0-9]+}} = ar.bsp
  %p = call ptr @llvm.ia64.bsp()
  call void @llvm.ia64.flushrs()
  %q = call ptr @llvm.ia64.bsp()
  %c = icmp eq ptr %p, %q
  ret i1 %c
}
