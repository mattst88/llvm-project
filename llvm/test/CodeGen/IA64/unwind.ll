; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A function says where its prologue ends, and of each instruction of it
; that saves the caller's frame state or the return link, or that makes the
; frame, that it does. An epilogue says where it gives the frame up.

declare i64 @ext(ptr)
declare void @stop() noreturn

define i64 @leaf(i64 %a) {
; CHECK-LABEL: leaf:
; CHECK-NEXT:  .prologue
; CHECK-NEXT:  .body
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .endp leaf
  %r = add i64 %a, 1
  ret i64 %r
}

define i64 @frame(i64 %a) {
; CHECK-LABEL: frame:
; CHECK-NEXT:  .prologue
; CHECK:       .fframe 304
; CHECK-NEXT:  adds r12 = -304, r12 ;;
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  adds r12 = 304, r12
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %p = alloca [40 x i64]
  %q = getelementptr i64, ptr %p, i64 %a
  store volatile i64 1, ptr %q
  %r = load volatile i64, ptr %p
  ret i64 %r
}

define i64 @call(i64 %a) {
; CHECK-LABEL: call:
; CHECK-NEXT:  .prologue
; CHECK:       .save ar.pfs, r34
; CHECK-NEXT:  alloc r34 = ar.pfs, 1, 3, 1, 0
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %p = alloca i64
  store i64 %a, ptr %p
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @large(i64 %a) {
; The size is the frame's, whatever the instruction that makes it adds.
; CHECK-LABEL: large:
; CHECK:       mov r2 = -800000 ;;
; CHECK-NEXT:  .fframe 800000
; CHECK-NEXT:  add r12 = r2, r12
; CHECK-NEXT:  .body
; CHECK:       mov r2 = 800000 ;;
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  add r12 = r2, r12
  %p = alloca [100000 x i64]
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @dynamic(i64 %n) {
; A frame of a size that is not known is found from the copy of the stack
; pointer that the prologue makes.
; CHECK-LABEL: dynamic:
; CHECK:       .vframe r36
; CHECK-NEXT:  mov r36 = r12
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  mov r12 = r36
  %p = alloca i64, i64 %n
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @aligned(i64 %n) {
; So is one that is aligned, and nothing is said of how far the stack
; pointer then moves.
; CHECK-LABEL: aligned:
; CHECK:       .vframe r35
; CHECK-NEXT:  mov r35 = r12
; CHECK-NEXT:  adds r12 = -112, r12 ;;
; CHECK-NEXT:  and r12 = -64, r12
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  mov r12 = r35
  %p = alloca i64, align 64
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @returns(i64 %a) {
; The code after an epilogue has the frame that the code before it had.
; CHECK-LABEL: returns:
; CHECK:       .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  .body
; CHECK-NOT:   .body
; CHECK:       .label_state 1
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB6_5:
; CHECK-NEXT:  .body
; CHECK-NEXT:  .copy_state 1
; CHECK-NEXT:  br.call.sptk.many b0 = stop ;;
  %p = alloca i64
  %c = icmp eq i64 %a, 1
  br i1 %c, label %one, label %other
one:
  %r = call i64 @ext(ptr %p)
  ret i64 %r
other:
  %d = icmp eq i64 %a, 2
  br i1 %d, label %two, label %three
two:
  %s = call i64 @ext(ptr null)
  %t = add i64 %s, 2
  ret i64 %t
three:
  call void @stop()
  unreachable
}
