; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=2 \
; RUN:   -verify-machineinstrs < %s 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=GISEL

declare void @llvm.init.trampoline(ptr, ptr, ptr)
declare ptr @llvm.adjust.trampoline(ptr)
declare i64 @nested(ptr nest, i64)
declare void @use(ptr)

; The static chain is in r15 and takes no slot: the argument after it is
; the first.
define i64 @callee(ptr nest %chain, i64 %a) {
; CHECK-LABEL: callee:
; CHECK:       ld8 r14 = [r15] ;;
; CHECK-NEXT:  add r8 = r14, r32
; GISEL-LABEL: callee:
; GISEL:       ld8 r14 = [r15] ;;
; GISEL-NEXT:  add r8 = r14, r32
  %v = load i64, ptr %chain
  %r = add i64 %v, %a
  ret i64 %r
}

define i64 @direct(ptr %chain, i64 %a) {
; CHECK-LABEL: direct:
; CHECK:       mov r37 = r33
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r15 = r32
; CHECK:       br.call.sptk.many b0 = nested ;;
; GISEL-LABEL: direct:
; GISEL-DAG:   mov r37 = r33
; GISEL-DAG:   mov r15 = r32
; GISEL:       br.call.sptk.many b0 = nested ;;
  %r = call i64 @nested(ptr nest %chain, i64 %a)
  ret i64 %r
}

define i64 @indirect(ptr %f, ptr %chain, i64 %a) {
; CHECK-LABEL: indirect:
; CHECK:       mov r38 = r34
; CHECK-NEXT:  mov r15 = r33
; CHECK:       br.call.sptk.many b0 = b6 ;;
  %r = call i64 %f(ptr nest %chain, i64 %a)
  ret i64 %r
}

; A trampoline is a function descriptor and two more doublewords. The entry
; point is the address of the code of __ia64_trampoline, not of a descriptor
; of it, and the gp is the address of the third doubleword, which is where
; the address of the descriptor of the function is. The static chain is the
; fourth. What is called is the address of the trampoline itself. GlobalISel
; leaves this function to SelectionDAG.
define void @make(ptr %chain) {
; CHECK-LABEL: make:
; CHECK:       .fframe 32
; CHECK-NEXT:  adds r12 = -32, r12 ;;
; CHECK:       adds r[[T:[0-9]+]] = 16, r12
; CHECK-DAG:   addl r[[F:[0-9]+]] = @ltoff(@fptr(callee)), r1
; CHECK-DAG:   addl r[[E:[0-9]+]] = @ltoff(__ia64_trampoline), r1
; CHECK-DAG:   adds r[[P3:[0-9]+]] = 24, r[[T]]
; CHECK-DAG:   st8 [r[[P3]]] = r32
; CHECK-DAG:   adds r[[P2:[0-9]+]] = 16, r[[T]]
; CHECK-DAG:   ld8 r[[FV:[0-9]+]] = [r[[F]]]
; CHECK-DAG:   st8 [r[[P2]]] = r[[FV]]
; CHECK-DAG:   adds r[[P1:[0-9]+]] = 8, r[[T]]
; CHECK-DAG:   st8 [r[[P1]]] = r[[P2]]
; CHECK-DAG:   ld8 r[[EV:[0-9]+]] = [r[[E]]]
; CHECK-DAG:   st8 [r[[T]]] = r[[EV]]
; CHECK:       br.call.sptk.many b0 = use ;;
  %t = alloca [32 x i8], align 8
  call void @llvm.init.trampoline(ptr %t, ptr @callee, ptr %chain)
  %f = call ptr @llvm.adjust.trampoline(ptr %t)
  call void @use(ptr %f)
  ret void
}
