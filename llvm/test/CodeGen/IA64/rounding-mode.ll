; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The rounding mode is bits 10 and 11 of ar.fpsr, where 0 is to nearest, 1
; down, 2 up and 3 toward zero. FLT_ROUNDS has them 1, 3, 2 and 0, which is
; 0x2d two bits at a time, and the other way they are 0x63.

define i32 @get() {
; CHECK-LABEL: get:
; CHECK:         mov r14 = ar.fpsr
; CHECK-NEXT:    mov r15 = 45 ;;
; CHECK-NEXT:    shr.u r14 = r14, 9 ;;
; CHECK-NEXT:    and r14 = 6, r14 ;;
; CHECK-NEXT:    shr.u r14 = r15, r14 ;;
; CHECK-NEXT:    and r8 = 3, r14
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = call i32 @llvm.get.rounding()
  ret i32 %r
}

define void @set(i32 %m) {
; CHECK-LABEL: set:
; CHECK:         mov r14 = ar.fpsr
; CHECK-NEXT:    dep.z r15 = r32, 1, 2
; CHECK-NEXT:    mov r16 = 99 ;;
; CHECK-NEXT:    shr.u r15 = r16, r15 ;;
; CHECK-NEXT:    dep r14 = r15, r14, 10, 2 ;;
; CHECK-NEXT:    mov ar.fpsr = r14
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  call void @llvm.set.rounding(i32 %m)
  ret void
}

define void @up() {
; CHECK-LABEL: up:
; CHECK:         mov r14 = ar.fpsr ;;
; CHECK-NEXT:    dep r14 = 0, r14, 10, 2 ;;
; CHECK-NEXT:    dep r14 = -1, r14, 11, 1 ;;
; CHECK-NEXT:    mov ar.fpsr = r14
  call void @llvm.set.rounding(i32 2)
  ret void
}

; A function that is called may change the mode, so what is read after it is
; not what was read before it.

declare void @change()

define i32 @twice() {
; CHECK-LABEL: twice:
; CHECK:         mov r{{[0-9]+}} = ar.fpsr
; CHECK:         br.call.sptk.many b0 = change ;;
; CHECK:         mov r{{[0-9]+}} = ar.fpsr
; CHECK:         br.ret.sptk.many b0 ;;
  %a = call i32 @llvm.get.rounding()
  call void @change()
  %b = call i32 @llvm.get.rounding()
  %r = add i32 %a, %b
  ret i32 %r
}

; An instruction that computes with the mode is in a later group than the
; one that sets it.

define double @then_add(double %a, double %b) strictfp {
; CHECK-LABEL: then_add:
; CHECK:         mov ar.fpsr = r14 ;;
; CHECK-NEXT:    fma.d f8 = f8, f1, f9
  call void @llvm.set.rounding(i32 2) strictfp
  %s = call double @llvm.experimental.constrained.fadd.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ret double %s
}
