; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A floating-point argument is in the next of f8 to f15, whatever slot it
; has, and a floating-point result in f8.
define double @second(i64 %a, double %b) {
; CHECK-LABEL: second:
; CHECK:       // %bb.0:
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret double %b
}

; Past the eighth slot it is in memory. A single is in the low half of its
; slot.
define float @ninth(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h, float %i, double %j) {
; CHECK-LABEL: ninth:
; CHECK:       adds r14 = 16, r12 ;;
; CHECK-NEXT:  ldfs f8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret float %i
}

define double @tenth(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h, float %i, double %j) {
; CHECK-LABEL: tenth:
; CHECK:       adds r14 = 24, r12 ;;
; CHECK-NEXT:  ldfd f8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret double %j
}

; The ninth is in memory although there were floating-point registers for
; only eight.
define double @ninth_double(double %a, double %b, double %c, double %d, double %e, double %f, double %g, double %h, double %i) {
; CHECK-LABEL: ninth_double:
; CHECK:       adds r14 = 16, r12 ;;
; CHECK-NEXT:  ldfd f8 = [r14]
  ret double %i
}

declare double @x(i64, float, i64, double)
declare double @y(i64, i64, i64, i64, i64, i64, i64, i64, float, double)

; The slots of the floating-point arguments are left alone: out1 and out3
; are not written. A constant is its bits, moved across.
define double @call() {
; CHECK-LABEL: call:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 3, 0
; CHECK:       movl r14 = 1073741824
; CHECK-NEXT:  movl r15 = 4616189618054758400
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  mov r35 = 1 ;;
; CHECK-NEXT:  .mmf
; CHECK-NEXT:  setf.s f8 = r14
; CHECK-NEXT:  setf.d f9 = r15
; CHECK-NEXT:  mov r37 = 3
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = x ;;
  %r = call double @x(i64 1, float 2.0, i64 3, double 4.0)
  ret double %r
}

define double @call_memory(float %f, double %d) {
; CHECK-LABEL: call_memory:
; CHECK:       adds r12 = -16, r12
; CHECK:       adds r14 = 24, r12
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r15 = 16, r12
; CHECK:       .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  stfd [r14] = f9
; CHECK-NEXT:  stfs [r15] = f8
; CHECK-NEXT:  br.call.sptk.many b0 = y ;;
  %r = call double @y(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, float %f, double %d)
  ret double %r
}

define float @load(ptr %p) {
; CHECK-LABEL: load:
; CHECK:       ldfs f8 = [r32]
  %v = load float, ptr %p
  ret float %v
}

define void @store(ptr %p, ptr %q, float %f, double %d) {
; CHECK-LABEL: store:
; CHECK:       stfs [r32] = f8
; CHECK-NEXT:  stfd [r33] = f9
  store float %f, ptr %p
  store double %d, ptr %q
  ret void
}

; f0 is 0.0 and f1 is 1.0, but there is no register that is -0.0.
define double @zero() {
; CHECK-LABEL: zero:
; CHECK:       mov f8 = f0
  ret double 0.0
}

define float @one() {
; CHECK-LABEL: one:
; CHECK:       mov f8 = f1
  ret float 1.0
}

define double @minus_zero() {
; CHECK-LABEL: minus_zero:
; CHECK:       movl r14 = -9223372036854775808 ;;
; CHECK-NEXT:  setf.d f8 = r14
  ret double -0.0
}

; A value that lives across a call is in a register the callee preserves,
; which is saved and restored whole.
define double @across(double %a) {
; CHECK-LABEL: across:
; CHECK:       adds r12 = -16, r12 ;;
; CHECK-NEXT:  adds r2 = 16, r12 ;;
; CHECK-NEXT:  .save.f 0x1
; CHECK-NEXT:  stf.spill [r2] = f2
; CHECK:       mov f2 = f8
; CHECK:       .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov r35 = 1
; CHECK-NEXT:  mov r37 = 3
; CHECK-NEXT:  br.call.sptk.many b0 = x ;;
; CHECK:       mov f8 = f2
; CHECK-NEXT:  adds r2 = 16, r12 ;;
; CHECK-NEXT:  ldf.fill f2 = [r2]
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
  %r = call double @x(i64 1, float 2.0, i64 3, double 4.0)
  ret double %a
}
