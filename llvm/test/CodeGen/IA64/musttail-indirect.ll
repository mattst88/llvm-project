; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; A function pointer is to a descriptor of the entry and the callee's global
; pointer. A call through one that has to be a branch loads both, and the
; callee finds its global pointer in r1 as it would after a call. This is how
; CPython's tail-calling interpreter goes from one handler to the next. A
; handler with no frame of its own hands on the one it has.

define ptr @leaf(ptr %f, ptr %t, i32 %op) {
; CHECK-LABEL: leaf:
; CHECK:         ld8 r14 = [r33] ;;
; CHECK-NEXT:    ld8 r15 = [r14], 8 ;;
; CHECK-NEXT:    ld8 r14 = [r14]
; CHECK-NEXT:    mov b6 = r15 ;;
; CHECK-NEXT:    mov r1 = r14
; CHECK-NEXT:    br.cond.sptk.many b6 ;;
; OBJ-LABEL:   <leaf>:
; OBJ:         br.cond.sptk.many b6
  %fn = load ptr, ptr %t
  %r = musttail call ptr %fn(ptr %f, ptr %t, i32 %op)
  ret ptr %r
}

declare void @g()

; preserve_none keeps the registers C does. What the function saved is
; restored before the branch, but a preserve_none callee is not one that
; counts on a frame of only its inputs, and it has the function's frame as it
; is, with the arguments where it looks for them.
define preserve_nonecc ptr @handler(ptr %f, ptr %t, i32 %op, i32 %arg) {
; CHECK-LABEL: handler:
; CHECK:         alloc r37 = ar.pfs, 4, 3, 0, 0
; CHECK:         br.call.sptk.many b0 = g ;;
; CHECK:         mov b0 = r38 ;;
; CHECK:         mov ar.pfs = r37 ;;
; CHECK:         ld8 r15 = [r14], 8 ;;
; CHECK-NEXT:    ld8 r14 = [r14]
; CHECK-NEXT:    mov b6 = r15 ;;
; CHECK-NEXT:    mov r1 = r14
; CHECK-NEXT:    br.cond.sptk.many b6 ;;
  call void @g()
  %i = zext i32 %op to i64
  %p = getelementptr ptr, ptr %t, i64 %i
  %fn = load ptr, ptr %p
  %r = musttail call preserve_nonecc ptr %fn(ptr %f, ptr %t, i32 %op, i32 %arg)
  ret ptr %r
}

