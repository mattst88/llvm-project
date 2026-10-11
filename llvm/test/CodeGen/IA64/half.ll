; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; Nothing reads, writes or converts a half-precision value: it is sixteen
; bits in a general register and in memory, and the library converts it.

; CHECK-LABEL: ext:
; CHECK:       ld2 r36 = [r32]
; CHECK:       br.call.sptk.many b0 = __extendhfsf2 ;;
define float @ext(ptr %p) nounwind {
  %h = load half, ptr %p
  %f = fpext half %h to float
  ret float %f
}

; CHECK-LABEL: trunc:
; CHECK:       br.call.sptk.many b0 = __truncdfhf2 ;;
; CHECK:       st2 [r33] = r8
define void @trunc(double %d, ptr %p) nounwind {
  %h = fptrunc double %d to half
  store half %h, ptr %p
  ret void
}

; CHECK-LABEL: arg:
; CHECK:       br.call.sptk.many b0 = __extendhfsf2 ;;
; CHECK:       br.call.sptk.many b0 = __extendhfsf2 ;;
; CHECK:       fma.s f8 = f8, f1, f2
; CHECK:       br.call.sptk.many b0 = __truncsfhf2 ;;
define half @arg(half %a, half %b) nounwind {
  %r = fadd half %a, %b
  ret half %r
}

; CHECK-LABEL: ext80:
; CHECK:       br.call.sptk.many b0 = __extendhfsf2 ;;
; CHECK:       fma f8 = f8, f1, f0
define x86_fp80 @ext80(half %h) nounwind {
  %f = fpext half %h to x86_fp80
  ret x86_fp80 %f
}

; CHECK-LABEL: toint:
; CHECK:       br.call.sptk.many b0 = __extendhfsf2 ;;
; CHECK:       fcvt.fx.trunc f6 = f8
define i32 @toint(half %h) nounwind {
  %i = fptosi half %h to i32
  ret i32 %i
}
