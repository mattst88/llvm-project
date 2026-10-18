; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A call that a function ends with is a branch where SelectionDAG makes it
; one: to a callee that is certain to be in this module, with every argument
; in a register, from a function that takes a fixed number of its own. The
; arguments are put in this function's input registers, and the frame is
; made over to be those and nothing else.

declare dso_local i64 @local(i64, i64)
declare dso_local double @local_fp(double, i64)
declare dso_local i64 @local9(i64, i64, i64, i64, i64, i64, i64, i64, i64)
declare i64 @extern(i64)

define i64 @leaf(i64 %a) {
; CHECK-LABEL: leaf:
; CHECK-DAG:   adds r32 = 1, r32
; CHECK-DAG:   mov r33 = 2
; CHECK:       alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:  br.cond.sptk.many local ;;
; CHECK-NEXT:  .endp leaf
  %b = add i64 %a, 1
  %r = tail call i64 @local(i64 %b, i64 2)
  ret i64 %r
}

define i64 @swap(i64 %a, i64 %b) {
; CHECK-LABEL: swap:
; CHECK:       mov r14 = r32
; CHECK-NEXT:  mov r32 = r33 ;;
; CHECK-NEXT:  mov r33 = r14 ;;
; CHECK:       alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:  br.cond.sptk.many local ;;
  %r = tail call i64 @local(i64 %b, i64 %a)
  ret i64 %r
}

; A floating-point argument has a slot, and the frame has the slot although
; nothing is in it.
define double @fp(i64 %a, double %d) {
; CHECK-LABEL: fp:
; CHECK:       alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:  br.cond.sptk.many local_fp ;;
  %r = tail call double @local_fp(double %d, i64 %a)
  ret double %r
}

; What was saved is put back first.
define i64 @after_call(i64 %a) {
; CHECK-LABEL: after_call:
; CHECK:       br.call.sptk.many b0 = extern ;;
; CHECK-DAG:   mov b0 = r{{[0-9]+}}
; CHECK-DAG:   mov ar.pfs = r{{[0-9]+}}
; CHECK:       alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:  br.cond.sptk.many local ;;
  %b = call i64 @extern(i64 %a)
  %r = tail call i64 @local(i64 %b, i64 %a)
  ret i64 %r
}

; One that has to be a branch is one.
define i64 @must(i64 %a, i64 %b) {
; CHECK-LABEL: must:
; CHECK:       alloc r2 = ar.pfs, 0, 0, 2, 0
; CHECK-NEXT:  br.cond.sptk.many local ;;
  %r = musttail call i64 @local(i64 %a, i64 %b)
  ret i64 %r
}

; A callee that may be in another module has its own global pointer, and
; one with an argument in memory would find this function's caller's there.
define i64 @other_module(i64 %a) {
; CHECK-LABEL: other_module:
; CHECK:       br.call.sptk.many b0 = extern ;;
  %r = tail call i64 @extern(i64 %a)
  ret i64 %r
}

define i64 @in_memory(i64 %a) {
; CHECK-LABEL: in_memory:
; CHECK:       br.call.sptk.many b0 = local9 ;;
  %r = tail call i64 @local9(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 %a)
  ret i64 %r
}

define i64 @variable(i64 %a, ...) {
; CHECK-LABEL: variable:
; CHECK:       br.call.sptk.many b0 = local ;;
  %r = tail call i64 @local(i64 %a, i64 1)
  ret i64 %r
}
