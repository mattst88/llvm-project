; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; A call that a function ends with can be a branch, to a callee that then
; returns to the function's caller. The callee has to find the registers and
; the stack as the function did: the arguments in the function's own input
; registers, the caller's registers restored, and a frame of the arguments
; and nothing else, which "alloc" makes of the one there is. That is the
; frame a call would have left it, and a callee that goes on to call without
; a frame of its own passes on what are then its outputs.

declare dso_local i64 @local(i64, i64)
declare dso_local double @local_fp(double, i64)
declare dso_local i64 @local9(i64, i64, i64, i64, i64, i64, i64, i64, i64)
declare dso_local i64 @local_byval(ptr byval([16 x i64]))
declare dso_local void @fill(ptr)
declare i64 @extern(i64)

; The second argument is in a register that was not an input, and the frame
; grows to hold it before it shrinks to the two.

define i64 @leaf(i64 %a) {
; CHECK-LABEL: leaf:
; CHECK:         .save ar.pfs, r34
; CHECK-NEXT:    alloc r34 = ar.pfs, 1, 2, 0, 0
; CHECK-NEXT:    .body
; CHECK-NEXT:    adds r32 = 1, r32
; CHECK-NEXT:    mov r33 = 2 ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
; CHECK-NEXT:    .endp leaf
  %b = add i64 %a, 1
  %r = tail call i64 @local(i64 %b, i64 2)
  ret i64 %r
}

; The arguments are where the callee's are, in the other order.

define i64 @swap(i64 %a, i64 %b) {
; CHECK-LABEL: swap:
; CHECK:         mov r14 = r32
; CHECK-NEXT:    mov r32 = r33 ;;
; CHECK-NEXT:    mov r33 = r14 ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
  %r = tail call i64 @local(i64 %b, i64 %a)
  ret i64 %r
}

; A floating-point argument is in its own register, and has a slot among the
; general ones all the same.

define double @fp(i64 %a, double %d) {
; CHECK-LABEL: fp:
; CHECK:         mov r33 = r32 ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local_fp ;;
  %r = tail call double @local_fp(double %d, i64 %a)
  ret double %r
}

; What the function saved is restored first, the stack pointer too, and the
; registers it was saved in are past the ones the arguments are in.

define i64 @frame(i64 %a) {
; CHECK-LABEL: frame:
; CHECK:         .save ar.pfs, r34
; CHECK-NEXT:    .mmi
; CHECK-NEXT:    alloc r34 = ar.pfs, 1, 3, 1, 0
; CHECK-NEXT:    .fframe 800
; CHECK-NEXT:    adds r12 = -800, r12 ;;
; CHECK-NEXT:    adds r36 = 16, r12
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = fill ;;
; CHECK-NEXT:    mov r33 = 3
; CHECK-NEXT:    mov b0 = r35
; CHECK-NEXT:    mov ar.pfs = r34
; CHECK-NEXT:    .restore sp
; CHECK-NEXT:    adds r12 = 800, r12 ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
; CHECK-NEXT:    .endp frame
  %p = alloca [100 x i64]
  call void @fill(ptr %p)
  %r = tail call i64 @local(i64 %a, i64 3)
  ret i64 %r
}

; The code after a branch away that is not the last of the function is
; described as the code before it was.

define i64 @middle(i64 %a) {
; CHECK-LABEL: middle:
; CHECK:         mov r33 = 3
; CHECK:         (p6) br.cond.dpnt.few .LBB4_2 ;;
; CHECK-NEXT:  // %bb.1: // %t
; CHECK-NEXT:    .label_state 1
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
; CHECK-NEXT:  .LBB4_2: // %e
; CHECK-NEXT:    .body
; CHECK-NEXT:    .copy_state 1
; CHECK-NEXT:    mov r36 = r32
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:    br.call.sptk.many b0 = extern ;;
  %c = icmp eq i64 %a, 0
  br i1 %c, label %t, label %e, !prof !0

t:
  %r = tail call i64 @local(i64 %a, i64 3)
  ret i64 %r

e:
  %x = call i64 @extern(i64 %a)
  %y = add i64 %x, 1
  ret i64 %y
}

; A callee that may be in another module may have another gp, which the
; function's caller does not expect to find when it has called one in its own.

define i64 @other_module(i64 %a) {
; CHECK-LABEL: other_module:
; CHECK:         br.call.sptk.many b0 = extern ;;
; CHECK:         br.ret.sptk.many b0 ;;
  %r = tail call i64 @extern(i64 %a)
  ret i64 %r
}

; So may one that is called through a pointer.

define i64 @pointer(ptr %f, i64 %a) {
; CHECK-LABEL: pointer:
; CHECK:         br.call.sptk.many b0 = b6 ;;
; CHECK:         br.ret.sptk.many b0 ;;
  %r = tail call i64 %f(i64 %a)
  ret i64 %r
}

; Arguments in memory would be in the caller's frame, which has no room that
; is the function's to use.

define i64 @memory(i64 %a) {
; CHECK-LABEL: memory:
; CHECK:         br.call.sptk.many b0 = local9 ;;
; CHECK:         br.ret.sptk.many b0 ;;
  %r = tail call i64 @local9(i64 %a, i64 2, i64 3, i64 4, i64 5, i64 6,
                             i64 7, i64 8, i64 9)
  ret i64 %r
}

define i64 @by_value(ptr %p) {
; CHECK-LABEL: by_value:
; CHECK:         br.call.sptk.many b0 = local_byval ;;
; CHECK:         br.ret.sptk.many b0 ;;
  %r = tail call i64 @local_byval(ptr byval([16 x i64]) %p)
  ret i64 %r
}

; A function with a variable argument list has all eight input registers,
; whatever it was called with.

define i64 @variadic(i64 %a, ...) {
; CHECK-LABEL: variadic:
; CHECK:         br.call.sptk.many b0 = local ;;
; CHECK:         br.ret.sptk.many b0 ;;
  %r = tail call i64 @local(i64 %a, i64 2)
  ret i64 %r
}

; An argument that nothing was put in is no reason for there not to be a
; register for it.

define i64 @undefined() {
; CHECK-LABEL: undefined:
; CHECK:         .body
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
  %r = tail call i64 @local(i64 undef, i64 undef)
  ret i64 %r
}

!0 = !{!"branch_weights", i32 1000, i32 1}

; A call that has to be a tail call can be one under the same conditions.

define i64 @must(i64 %a, i64 %b) {
; CHECK-LABEL: must:
; CHECK:         alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
  %r = musttail call i64 @local(i64 %a, i64 %b)
  ret i64 %r
}

; The frame "alloc" leaves has only the arguments in it, so whatever reads a
; register beyond them comes before it.

define i64 @shrinks(i64 %a, i64 %b, i64 %c, i64 %d, ptr %p) {
; CHECK-LABEL: shrinks:
; CHECK:         ld8 r14 = [r36]
; CHECK-NEXT:    mov r32 = r33 ;;
; CHECK-NEXT:    add r33 = r14, r35 ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:    br.cond.sptk.many local ;;
  %x = load i64, ptr %p
  %y = add i64 %x, %d
  %r = tail call i64 @local(i64 %b, i64 %y)
  ret i64 %r
}
