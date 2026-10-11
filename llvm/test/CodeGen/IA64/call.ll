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
; CHECK-NEXT:  mov r36 = b0
; CHECK-NEXT:  mov r34 = r1
; CHECK-NEXT:  mov r37 = r33
; CHECK-NEXT:  mov r38 = r32 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  add r8 = r8, r32
; CHECK-NEXT:  mov b0 = r36
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
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  mov r35 = r32 ;;
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
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  mov r33 = r1 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = vext ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  adds r14 = 24, r12 ;;
; CHECK-NEXT:  st8 [r14] = r32
; CHECK:       br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
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
; CHECK-NEXT:  mov r35 = b0
; CHECK:       br.call.sptk.many b0 = vext ;;
; CHECK:       mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
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
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = vext ;;
; CHECK-NEXT:  mov r1 = r32
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  tail call void @vext()
  ret void
}
