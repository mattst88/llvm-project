; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A call replaces the return link and the previous function state, so the
; caller keeps both in locals. The arguments go in the output registers, and
; the global pointer is put back after a call that may leave this module.

declare i64 @ext(i64, i64)
declare void @vext()

define internal i64 @local(i64 %a) noinline {
; CHECK-LABEL: local:
; CHECK:       adds r8 = 1, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = add i64 %a, 1
  ret i64 %r
}

define i64 @args(i64 %a, i64 %b) {
; CHECK-LABEL: args:
; CHECK:       alloc r35 = ar.pfs, 2, 3, 2, 0
; CHECK-NEXT:  mov r34 = r1
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r37 = r33
; CHECK-NEXT:  mov r38 = r32
; CHECK-NEXT:  .save rp, r36
; CHECK-NEXT:  mov r36 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  add r8 = r8, r32
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @ext(i64 %b, i64 %a)
  %s = add i64 %r, %a
  ret i64 %s
}

; The callee is in this module, and leaves the global pointer as it is.
define i64 @same_module(i64 %a) {
; CHECK-LABEL: same_module:
; CHECK:       alloc r33 = ar.pfs, 1, 2, 1, 0
; CHECK-NEXT:  mov r35 = r32
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = local ;;
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @local(i64 %a)
  ret i64 %r
}

; The sixteen bytes at the stack pointer are the callee's, so the object is
; above them, in the sixteen bytes this function was given.
define i64 @frame(i64 %a) {
; CHECK-LABEL: frame:
; CHECK:       alloc r34 = ar.pfs, 1, 3, 2, 0
; CHECK-NEXT:  .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  br.call.sptk.many b0 = vext ;;
; CHECK-NEXT:  adds r14 = 24, r12
; CHECK-NEXT:  mov r36 = r32
; CHECK-NEXT:  mov r37 = r0
; CHECK-NEXT:  mov r1 = r33 ;;
; CHECK-NEXT:  st8 [r14] = r32
; CHECK-NEXT:  br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  call void @vext()
  %p = alloca i64
  store i64 %a, ptr %p
  %r = call i64 @ext(i64 %a, i64 0)
  ret i64 %r
}

; What ar.pfs and b0 were saved in is live wherever the function may yet
; return from.
define void @later(i64 %a) {
; CHECK-LABEL: later:
; CHECK:       alloc r34 = ar.pfs, 1, 3, 0, 0
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK:       mov r33 = r1
; CHECK:       br.call.sptk.many b0 = vext ;;
; CHECK:       mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34{{$}}
; CHECK-NEXT:  .LBB4_2:
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, 0
  br i1 %c, label %skip, label %do
do:
  call void @vext()
  br label %skip
skip:
  ret void
}

; There are no tail calls yet.
define void @tail() {
; CHECK-LABEL: tail:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 0, 0
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = vext ;;
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov r1 = r32
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  tail call void @vext()
  ret void
}

; A pointer to a function is the address of its descriptor, which has the
; entry point and the global pointer the function expects. The global pointer
; is loaded into r1 itself, so the call need not wait for a copy of it.
define i64 @indirect(ptr %f, i64 %a) {
; CHECK-LABEL: indirect:
; CHECK:       alloc r35 = ar.pfs, 2, 3, 1, 0
; CHECK-NEXT:  ld8 r14 = [r32], 8
; CHECK-NEXT:  mov r34 = r1
; CHECK-NEXT:  mov r37 = r33 ;;
; CHECK-NEXT:  ld8 r1 = [r32]
; CHECK-NEXT:  .save rp, r36
; CHECK-NEXT:  mov r36 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov b6 = r14
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 %f(i64 %a)
  ret i64 %r
}

; The address of a variable is not the name of a function to branch to. What
; the variable holds is the descriptor.

@descriptors = external global [4 x ptr]

define i64 @variable(i64 %a) {
; CHECK-LABEL: variable:
; CHECK:       addl r14 = @ltoffx(descriptors), r1
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  mov r36 = r32 ;;
; CHECK-NEXT:  ld8.mov r14 = [r14], descriptors ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  ld8 r15 = [r14], 8
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  ld8 r1 = [r14]
; CHECK-NEXT:  mov b6 = r15
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
  %r = call i64 @descriptors(i64 %a)
  ret i64 %r
}

define i64 @variable_offset(i64 %a) {
; CHECK-LABEL: variable_offset:
; CHECK:       addl r14 = @ltoffx(descriptors), r1
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  mov r36 = r32 ;;
; CHECK-NEXT:  ld8.mov r14 = [r14], descriptors ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r15 = 24, r14
; CHECK-NEXT:  adds r14 = 16, r14 ;;
; CHECK-NEXT:  ld8 r1 = [r15]
; CHECK-NEXT:  ld8 r14 = [r14]
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov b6 = r14
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
  %r = call i64 getelementptr inbounds ([4 x ptr], ptr @descriptors, i64 0, i64 2)(i64 %a)
  ret i64 %r
}

; The global pointer stays where it was loaded, and is copied to r1, when the
; call passes it on as well or r1 is read after it is loaded.
define i64 @gp_argument(ptr %f) {
; CHECK-LABEL: gp_argument:
; CHECK:       ld8 r36 = [r32]
; CHECK:       mov r1 = r36
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
  %p = getelementptr i8, ptr %f, i64 8
  %gp = load i64, ptr %p
  %r = call i64 %f(i64 %gp)
  ret i64 %r
}

@v = external global i64

define i64 @gp_between(ptr %f) {
; CHECK-LABEL: gp_between:
; CHECK:       addl r16 = @ltoffx(v), r1
; CHECK:       ld8 r15 = [r32]
; CHECK:       mov r1 = r15
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
  %r = call i64 %f(ptr @v)
  ret i64 %r
}

; A call returns to the bundle after it, and an unwinder has to find that
; address in the function that made the call, so a call is never the last
; instruction of a function.

declare void @never() noreturn

define internal void @inside() noreturn noinline {
; CHECK-LABEL: inside:
; CHECK:         br.call.sptk.many b0 = never ;;
; CHECK-NEXT:    mov r1 = r32 ;;
; CHECK-NEXT:    .endp inside
  call void @never()
  unreachable
}

define void @ends_with_call() {
; CHECK-LABEL: ends_with_call:
; CHECK:         br.call.sptk.many b0 = inside ;;
; CHECK-NEXT:    break.f 0 ;;
; CHECK-NEXT:    .endp ends_with_call
  call void @inside()
  unreachable
}
