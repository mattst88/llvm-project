; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; There is one instruction, which multiplies and adds. A sum is a product
; with f1 and a product a sum with f0, and the completer is the precision
; that the result is rounded to.
define double @add(double %a, double %b) {
; CHECK-LABEL: add:
; CHECK:       fma.d f8 = f8, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fadd double %a, %b
  ret double %r
}

define float @adds(float %a, float %b) {
; CHECK-LABEL: adds:
; CHECK:       fma.s f8 = f8, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fadd float %a, %b
  ret float %r
}

define double @sub(double %a, double %b) {
; CHECK-LABEL: sub:
; CHECK:       fms.d f8 = f8, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fsub double %a, %b
  ret double %r
}

define float @subs(float %a, float %b) {
; CHECK-LABEL: subs:
; CHECK:       fms.s f8 = f8, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fsub float %a, %b
  ret float %r
}

define double @mul(double %a, double %b) {
; CHECK-LABEL: mul:
; CHECK:       fma.d f8 = f8, f9, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fmul double %a, %b
  ret double %r
}

define float @muls(float %a, float %b) {
; CHECK-LABEL: muls:
; CHECK:       fma.s f8 = f8, f9, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fmul float %a, %b
  ret float %r
}

define double @nmul(double %a, double %b) {
; CHECK-LABEL: nmul:
; CHECK:       fnma.d f8 = f8, f9, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %m = fmul double %a, %b
  %r = fneg double %m
  ret double %r
}

define double @fma(double %a, double %b, double %c) {
; CHECK-LABEL: fma:
; CHECK:       fma.d f8 = f8, f9, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call double @llvm.fma.f64(double %a, double %b, double %c)
  ret double %r
}

define float @fmas(float %a, float %b, float %c) {
; CHECK-LABEL: fmas:
; CHECK:       fma.s f8 = f8, f9, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

define double @fms(double %a, double %b, double %c) {
; CHECK-LABEL: fms:
; CHECK:       fms.d f8 = f8, f9, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %n = fneg double %c
  %r = call double @llvm.fma.f64(double %a, double %b, double %n)
  ret double %r
}

define double @fnma(double %a, double %b, double %c) {
; CHECK-LABEL: fnma:
; CHECK:       fnma.d f8 = f8, f9, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %n = fneg double %a
  %r = call double @llvm.fma.f64(double %n, double %b, double %c)
  ret double %r
}

; A product and a sum are one instruction only where one rounding instead
; of two is allowed.
define double @muladd(double %a, double %b, double %c) {
; CHECK-LABEL: muladd:
; CHECK:       fma.d f6 = f8, f9, f0 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %m = fmul double %a, %b
  %r = fadd double %m, %c
  ret double %r
}

define double @muladd_contract(double %a, double %b, double %c) {
; CHECK-LABEL: muladd_contract:
; CHECK:       fma.d f8 = f8, f9, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %m = fmul contract double %a, %b
  %r = fadd contract double %m, %c
  ret double %r
}

; The sign is merged in from another register: the value itself inverted,
; or f0, where it is clear.
define double @neg(double %a) {
; CHECK-LABEL: neg:
; CHECK:       fmerge.ns f8 = f8, f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fneg double %a
  ret double %r
}

define float @abs(float %a) {
; CHECK-LABEL: abs:
; CHECK:       fmerge.s f8 = f0, f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}

define double @nabs(double %a) {
; CHECK-LABEL: nabs:
; CHECK:       fmerge.ns f8 = f0, f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %b = call double @llvm.fabs.f64(double %a)
  %r = fneg double %b
  ret double %r
}

define double @cps(double %a, double %b) {
; CHECK-LABEL: cps:
; CHECK:       fmerge.s f8 = f9, f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call double @llvm.copysign.f64(double %a, double %b)
  ret double %r
}

; There is no instruction that divides or takes a square root.
define double @div(double %a, double %b) {
; CHECK-LABEL: div:
; CHECK:       br.call.sptk.many b0 = __divdf3 ;;
  %r = fdiv double %a, %b
  ret double %r
}

define float @divs(float %a, float %b) {
; CHECK-LABEL: divs:
; CHECK:       br.call.sptk.many b0 = __divsf3 ;;
  %r = fdiv float %a, %b
  ret float %r
}

define double @rem(double %a, double %b) {
; CHECK-LABEL: rem:
; CHECK:       br.call.sptk.many b0 = fmod ;;
  %r = frem double %a, %b
  ret double %r
}

define double @sqrt(double %a) {
; CHECK-LABEL: sqrt:
; CHECK:       br.call.sptk.many b0 = sqrt ;;
  %r = call double @llvm.sqrt.f64(double %a)
  ret double %r
}

; Nor one for what the mathematical library does.
define double @sin(double %a) {
; CHECK-LABEL: sin:
; CHECK:       br.call.sptk.many b0 = sin ;;
  %r = call double @llvm.sin.f64(double %a)
  ret double %r
}

define float @cos(float %a) {
; CHECK-LABEL: cos:
; CHECK:       br.call.sptk.many b0 = cosf ;;
  %r = call float @llvm.cos.f32(float %a)
  ret float %r
}

define double @pow(double %a, double %b) {
; CHECK-LABEL: pow:
; CHECK:       br.call.sptk.many b0 = pow ;;
  %r = call double @llvm.pow.f64(double %a, double %b)
  ret double %r
}

define float @floor(float %a) {
; CHECK-LABEL: floor:
; CHECK:       br.call.sptk.many b0 = floorf ;;
  %r = call float @llvm.floor.f32(float %a)
  ret float %r
}

define double @two(double %a) {
; CHECK-LABEL: two:
; CHECK:       fma.d f8 = f8, f1, f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fmul double %a, 2.0
  ret double %r
}
