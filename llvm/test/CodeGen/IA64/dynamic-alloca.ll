; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

declare void @g(ptr, i64)

; The stack pointer moves down by the size, and the scratch area of the
; callee is still at the stack pointer, so what was allocated starts above
; it. A local holds the stack pointer to put back.
define void @f(i64 %n) {
; CHECK-LABEL: f:
; CHECK:       alloc r33 = ar.pfs, 1, 3, 2, 0
; CHECK-NEXT:  .vframe r35
; CHECK-NEXT:  mov r35 = r12
; CHECK-NEXT:  adds r14 = 15, r32
; CHECK-NEXT:  mov r37 = r32
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r36 = 16, r12
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r32
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  mov r12 = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  ret void
}

; The objects of the frame are found from that local.
define i64 @fixed(i64 %n, i64 %m) {
; CHECK-LABEL: fixed:
; CHECK:       alloc r36 = ar.pfs, 2, 5, 2, 0
; CHECK-NEXT:  .vframe r38
; CHECK-NEXT:  mov r38 = r12
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  adds r14 = 15, r32 ;;
; CHECK-NEXT:  adds r34 = 8, r38
; CHECK-NEXT:  mov r35 = r1
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK-NEXT:  st8 [r34] = r33
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  .save rp, r37
; CHECK-NEXT:  mov r37 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  adds r39 = 16, r12
; CHECK-NEXT:  mov r40 = r32
; CHECK:       ld8 r8 = [r34]
; CHECK-NEXT:  mov b0 = r37
; CHECK-NEXT:  mov ar.pfs = r36
; CHECK-NEXT:  mov r1 = r35
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  mov r12 = r38
  %x = alloca i64
  store i64 %m, ptr %x
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  call void @g(ptr %x, i64 %n)
  %r = load i64, ptr %x
  ret i64 %r
}

; With no call there is nothing below it. Memory below sp may be overwritten
; by a signal handler at any instruction, so sp is put back after the load
; from the space it frees, though they are in one group.
define i8 @leaf(i64 %n) {
; CHECK-LABEL: leaf:
; CHECK:       alloc r33 = ar.pfs, 1, 2, 0, 0
; CHECK-NEXT:  .vframe r34
; CHECK-NEXT:  mov r34 = r12
; CHECK:       sub r12 = r12, r14
; CHECK-NEXT:  mov r15 = 1 ;;
; CHECK-NEXT:  mov r14 = r12 ;;
; CHECK-NEXT:  st1 [r14] = r15
; CHECK-NEXT:  ld1 r8 = [r14]
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  mov r12 = r34
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
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r14 = 32, r12
; CHECK-NEXT:  adds r15 = 24, r12
; CHECK:       st8 [r15] = r14
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
; CHECK-NEXT:  and r37 = -64, r14
  %a = alloca i8, i64 %n, align 64
  call void @g(ptr %a, i64 %n)
  ret void
}

; The stack pointer is saved and put back like any other value.
declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)
define void @loop(i64 %n) {
; CHECK-LABEL: loop:
; CHECK:       mov r34 = r12 ;;
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK-NEXT:  sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r39 = 16, r12
; CHECK-NEXT:  mov r40 = r32
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  .mlx
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r12 = r34
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

declare i64 @llvm.get.dynamic.area.offset.i64()
declare void @many(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64)

; What was allocated last starts above the scratch area, and above the
; arguments that calls pass in memory when there are any.
define i64 @area(i64 %n) {
; CHECK-LABEL: area:
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK:       mov r8 = 16
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  %o = call i64 @llvm.get.dynamic.area.offset.i64()
  ret i64 %o
}

define i64 @area_stack_args(i64 %n) {
; CHECK-LABEL: area_stack_args:
; CHECK:       br.call.sptk.many b0 = many ;;
; CHECK:       mov r8 = 48
  %a = alloca i8, i64 %n
  call void @g(ptr %a, i64 %n)
  call void @many(i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 9, i64 10)
  %o = call i64 @llvm.get.dynamic.area.offset.i64()
  ret i64 %o
}

; The arguments of a call can take more room than an instruction that adds
; to the stack pointer can name.
%big = type { [20000 x i8] }
declare void @take(ptr byval(%big) align 8, ptr)
define void @big_area(ptr %b, i64 %n) {
; CHECK-LABEL: big_area:
; CHECK:       sub r12 = r12, r14
; CHECK:       mov r41 = 19968
; CHECK:       add r41 = r41, r12
; CHECK:       st8 [r14] = r41
; CHECK-NEXT:  br.call.sptk.many b0 = take ;;
  %a = alloca i8, i64 %n, align 16
  call void @take(ptr byval(%big) align 8 %b, ptr %a)
  ret void
}
