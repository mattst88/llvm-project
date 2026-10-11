; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

declare void @g(ptr, i64)

; The stack pointer moves down by the size, and the scratch area of the
; callee is still at the stack pointer, so what was allocated starts above
; it. A local holds the stack pointer to put back.
define void @f(i64 %n) {
; CHECK-LABEL: f:
; CHECK:       alloc r34 = ar.pfs, 1, 4, 2, 0
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  mov r36 = r12
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  mov r38 = r32 ;;
; CHECK-NEXT:  adds r14 = 15, r38 ;;
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r37 = 16, r12
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r12 = r36
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  ret void
}

; The objects of the frame are found from that local.
define i64 @fixed(i64 %n, i64 %m) {
; CHECK-LABEL: fixed:
; CHECK:       alloc r36 = ar.pfs, 2, 5, 2, 0
; CHECK-NEXT:  mov r37 = b0
; CHECK-NEXT:  mov r38 = r12
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  mov r35 = r1 ;;
; CHECK-NEXT:  adds r34 = 8, r38 ;;
; CHECK-NEXT:  st8 [r34] = r33
; CHECK:       sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r39 = 16, r12
; CHECK:       ld8 r8 = [r34]
; CHECK-NEXT:  mov r12 = r38
  %x = alloca i64
  store i64 %m, ptr %x
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  call void @g(ptr %x, i64 %n)
  %r = load i64, ptr %x
  ret i64 %r
}

; With no call there is nothing below it.
define i8 @leaf(i64 %n) {
; CHECK-LABEL: leaf:
; CHECK:       alloc r33 = ar.pfs, 1, 2, 0, 0
; CHECK-NEXT:  mov r34 = r12
; CHECK:       sub r12 = r12, r14
; CHECK:       mov r15 = r12 ;;
; CHECK-NEXT:  st1 [r15] = r14
; CHECK:       mov r12 = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %a = alloca i8, i64 %n
  store volatile i8 1, ptr %a
  %r = load volatile i8, ptr %a
  ret i8 %r
}

; Arguments in memory are below it too.
declare void @h(i64, i64, i64, i64, i64, i64, i64, i64, i64, ptr)
define void @stack_arguments(i64 %n) {
; CHECK-LABEL: stack_arguments:
; CHECK:       sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r14 = 32, r12
; CHECK-NEXT:  adds r15 = 24, r12 ;;
; CHECK-NEXT:  st8 [r15] = r14
  %a = alloca i8, i64 %n
  call void @h(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 9, ptr %a)
  ret void
}

; More alignment than the stack pointer has is made of more space.
define void @aligned(i64 %n) {
; CHECK-LABEL: aligned:
; CHECK:       sub r14 = r12, r14 ;;
; CHECK-NEXT:  adds r12 = -48, r14 ;;
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  adds r14 = 63, r14 ;;
; CHECK-NEXT:  and r38 = -64, r14
  %a = alloca i8, i64 %n, align 64
  call void @g(ptr %a, i64 %n)
  ret void
}

; The stack pointer is saved and put back like any other value.
declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)
define void @loop(i64 %n) {
; CHECK-LABEL: loop:
; CHECK:       mov r35 = r12 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r39 = 16, r12
; CHECK-NEXT:  mov r40 = r33
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov r12 = r35
entry:
  br label %l
l:
  %i = phi i64 [0, %entry], [%j, %l]
  %s = call ptr @llvm.stacksave()
  %a = alloca i8, i64 %i
  call void @g(ptr %a, i64 %i)
  call void @llvm.stackrestore(ptr %s)
  %j = add i64 %i, 1
  %c = icmp eq i64 %j, %n
  br i1 %c, label %x, label %l
x:
  ret void
}
