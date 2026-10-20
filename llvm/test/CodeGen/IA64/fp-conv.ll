; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A floating-point register converts a value to an integer in place, which
; is then its significand and crosses to a general register as that.
define i64 @fptosi(double %a) {
; CHECK-LABEL: fptosi:
; CHECK:       fcvt.fx.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptosi double %a to i64
  ret i64 %r
}

define i64 @fptoui(double %a) {
; CHECK-LABEL: fptoui:
; CHECK:       fcvt.fxu.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptoui double %a to i64
  ret i64 %r
}

define i64 @fptosis(float %a) {
; CHECK-LABEL: fptosis:
; CHECK:       fcvt.fx.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptosi float %a to i64
  ret i64 %r
}

; A narrower integer is the low bits of a wide one.
define signext i32 @fptosi32(float %a) {
; CHECK-LABEL: fptosi32:
; CHECK:       fcvt.fx.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptosi float %a to i32
  ret i32 %r
}

define zeroext i32 @fptoui32(double %a) {
; CHECK-LABEL: fptoui32:
; CHECK:       fcvt.fxu.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptoui double %a to i32
  ret i32 %r
}

; The other way the significand is exact and is then rounded to the
; precision of the result.
define double @sitofp(i64 %a) {
; CHECK-LABEL: sitofp:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i64 %a to double
  ret double %r
}

define float @sitofps(i64 %a) {
; CHECK-LABEL: sitofps:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK-NEXT:  fma.s f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i64 %a to float
  ret float %r
}

; A significand that has no sign is a number as it stands.
define double @uitofp(i64 %a) {
; CHECK-LABEL: uitofp:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = uitofp i64 %a to double
  ret double %r
}

define float @uitofps(i64 %a) {
; CHECK-LABEL: uitofps:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fma.s f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = uitofp i64 %a to float
  ret float %r
}

; An integer of no more bits than the result's significand is exact in it,
; and needs no rounding.
define double @sitofp32(i32 %a) {
; CHECK-LABEL: sitofp32:
; CHECK:       sxt4 r14 = r32 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fcvt.xf f8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i32 %a to double
  ret double %r
}

define double @sitofp54(i64 %a) {
; CHECK-LABEL: sitofp54:
; CHECK:       shr r14 = r32, 10 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fcvt.xf f8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %s = ashr i64 %a, 10
  %r = sitofp i64 %s to double
  ret double %r
}

define double @sitofp55(i64 %a) {
; CHECK-LABEL: sitofp55:
; CHECK:       shr r14 = r32, 9 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %s = ashr i64 %a, 9
  %r = sitofp i64 %s to double
  ret double %r
}

define float @sitofps16(i16 %a) {
; CHECK-LABEL: sitofps16:
; CHECK:       sxt2 r14 = r32 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fcvt.xf f8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i16 %a to float
  ret float %r
}

define float @sitofps32(i32 %a) {
; CHECK-LABEL: sitofps32:
; CHECK:       sxt4 r14 = r32 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK-NEXT:  fma.s f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i32 %a to float
  ret float %r
}

define double @uitofp32(i32 %a) {
; CHECK-LABEL: uitofp32:
; CHECK:       zxt4 r14 = r32 ;;
; CHECK-NEXT:  setf.sig f6 = r14 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = uitofp i32 %a to double
  ret double %r
}

; A register holds a value of either precision in the one format, but a
; single that is not a normal number is not one in the register, and stfd and
; getf.d write a double from it that is another number. So a value is made
; a normal number where it becomes a double, as GCC makes it, and a wider one
; is rounded.
define double @fpext(float %a) {
; CHECK-LABEL: fpext:
; CHECK:       fma.d f8 = f8, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fpext float %a to double
  ret double %r
}

; A single that is read to be a double is no different.
define void @fpext_load(ptr %p, ptr %q) {
; CHECK-LABEL: fpext_load:
; CHECK:       ldfs f6 = [r32] ;;
; CHECK-NEXT:  fma.d f6 = f6, f1, f0 ;;
; CHECK-NEXT:  stfd [r33] = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %a = load float, ptr %p
  %r = fpext float %a to double
  store double %r, ptr %q
  ret void
}

define float @fptrunc(double %a) {
; CHECK-LABEL: fptrunc:
; CHECK:       fma.s f8 = f8, f1, f0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fptrunc double %a to float
  ret float %r
}

; The bits of a value are those of its format in memory.
define i64 @bits(double %a) {
; CHECK-LABEL: bits:
; CHECK:       getf.d r8 = f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = bitcast double %a to i64
  ret i64 %r
}

define double @from_bits(i64 %a) {
; CHECK-LABEL: from_bits:
; CHECK:       setf.d f8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = bitcast i64 %a to double
  ret double %r
}

define i32 @bitss(float %a) {
; CHECK-LABEL: bitss:
; CHECK:       getf.s r8 = f8
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = bitcast float %a to i32
  ret i32 %r
}

define float @from_bitss(i32 %a) {
; CHECK-LABEL: from_bitss:
; CHECK:       setf.s f8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = bitcast i32 %a to float
  ret float %r
}

; An integer of two registers is converted in the library.
define i128 @fptosi128(double %a) {
; CHECK-LABEL: fptosi128:
; CHECK:       br.call.sptk.many b0 = __fixdfti ;;
  %r = fptosi double %a to i128
  ret i128 %r
}

define double @sitofp128(i128 %a) {
; CHECK-LABEL: sitofp128:
; CHECK:       br.call.sptk.many b0 = __floattidf ;;
  %r = sitofp i128 %a to double
  ret double %r
}
