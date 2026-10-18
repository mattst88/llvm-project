; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,SDAG
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,GISEL

; A vector of two single-precision values is what GCC has in a
; floating-point register as the significand of what the register holds,
; and not as a value that arithmetic is done on.

declare <2 x float> @ext(i64, <2 x float>, double, <2 x float> inreg, double)
declare void @var(i64, ...)
declare void @mem(i64, i64, i64, i64, i64, i64, i64, <2 x float>, <2 x float>)

; It has a slot, and is in the next of f8 to f15.
define <2 x float> @arg(i64 %a, <2 x float> %v, double %d) {
; CHECK-LABEL: arg:
; CHECK:       getf.sig r14 = f8
; CHECK:       setf.sig f8 = r14
; CHECK-NEXT:  br.ret.sptk.many b0
  %r = fadd <2 x float> %v, %v
  ret <2 x float> %r
}

; One that is marked as in a register has a register after its own that
; nothing else is in: the second of the two that GCC counts for a
; single-precision complex value it passes this way.
define <2 x float> @call(<2 x float> %v) {
; CHECK-LABEL: call:
; CHECK:       getf.sig r14 = f8
; CHECK-DAG:   mov f9 = f1
; CHECK-DAG:   setf.d f12 = r15
; CHECK-DAG:   setf.sig f8 = r14
; CHECK:       mov f10 = f8
; CHECK-NEXT:  .save rp, r36
; CHECK-NEXT:  mov r36 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = ext
; CHECK-NEXT:  getf.sig r33 = f8
  %r = call <2 x float> @ext(i64 1, <2 x float> %v, double 1.0, <2 x float> %v, double 2.0)
; One that the callee has not named is in its slot.
; CHECK:       mov r38 = r33
; CHECK-NEXT:  br.call.sptk.many b0 = var
  call void (i64, ...) @var(i64 1, <2 x float> %r)
  ret <2 x float> %r
}

; One that has no slot that is a register is in memory.
define void @stack(<2 x float> %v) {
; CHECK-LABEL: stack:
; CHECK:       getf.sig [[V:r[0-9]+]] = f8
; CHECK-DAG:   adds [[A:r[0-9]+]] = 16, r12
; CHECK-DAG:   setf.sig f8 = [[V]]
; CHECK:       st8 {{\[}}[[A]]{{\]}} = [[V]]
; CHECK:       br.call.sptk.many b0 = mem
  call void @mem(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, <2 x float> %v, <2 x float> %v)
  ret void
}

define float @stack_callee(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, <2 x float> %v, <2 x float> %w) {
; CHECK-LABEL: stack_callee:
; CHECK:       adds [[A:r[0-9]+]] = 16, r12
; SDAG:        ldfs f8 = {{\[}}[[A]]{{\]}}
; GISEL:       ld8 [[B:r[0-9]+]] = {{\[}}[[A]]{{\]}}
; GISEL:       setf.s f8 = [[B]]
  %r = extractelement <2 x float> %w, i32 0
  ret float %r
}

; The words of a result are in f8 and the registers after it.
define [2 x <2 x float>] @result(<2 x float> %v) {
; CHECK-LABEL: result:
; CHECK:       getf.sig r14 = f8
; CHECK:       setf.sig f8 = r14
; CHECK:       mov f9 = f8
; CHECK-NEXT:  br.ret.sptk.many b0
  %a = insertvalue [2 x <2 x float>] poison, <2 x float> %v, 0
  %b = insertvalue [2 x <2 x float>] %a, <2 x float> %v, 1
  ret [2 x <2 x float>] %b
}

; -O0 goes by way of SelectionDAG for these.
define <2 x float> @fast(<2 x float> %v) "frame-pointer"="none" {
; CHECK-LABEL: fast:
; CHECK:       br.ret.sptk.many b0
  ret <2 x float> %v
}

declare void @named(<2 x float>, ...)

; One that a callee with a variable number of arguments has named is in
; the register and in its slot.
define void @variadic(<2 x float> %v) {
; CHECK-LABEL: variadic:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 1, 0
; CHECK:       getf.sig r35 = f8
; CHECK:       setf.sig f8 = r35
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = named
  call void (<2 x float>, ...) @named(<2 x float> %v)
  ret void
}
