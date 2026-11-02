; RUN: llc -mtriple=ia64-unknown-linux-gnu -ia64-fp-anti-dep-breaking=0 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -ia64-fp-anti-dep-breaking=0 -ia64-sqrt-throughput=0 < %s \
; RUN:   | FileCheck %s --check-prefix=LATENCY

; There is no instruction that takes a square root. frsqrta gives the
; reciprocal of the root to under nine bits, and a predicate that is clear when
; the operand is a zero, an infinity, a NaN or negative and what it gave is the
; root. From there the steps are those of gcc and glibc, in status field 1 so
; that no flag of a step is one the program sees, and the last adds what the
; remainder says is missing and rounds once, as the program asked. A root
; that is returned is written to f8 by that last step, which leaves in place
; a copy of what frsqrta gave, made once the operand in f8 is dead.
;
; A double root takes the shorter sequence aCC uses, one step longer. The
; sequence glibc uses gives a better 0.5 / sqrt(x), and fp-div.ll has a division
; that starts from it.

define float @sqrt_float(float %a) {
; CHECK-LABEL: sqrt_float:
; CHECK:         frsqrta f6, p8 = f8
; CHECK-NEXT:    movl r14 = 4600427019358961664
; CHECK-NEXT:    movl r15 = 4602678819172646912 ;;
; CHECK-NEXT:    setf.d f9 = r14
; CHECK-NEXT:    setf.d f10 = r15
; CHECK-NEXT:    (p8) fma.s1 f7 = f8, f6, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f7, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f7, f10
; CHECK-NEXT:    (p8) fma.s1 f7 = f6, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f9, f6 ;;
; CHECK-NEXT:    (p8) fma.s.s1 f9 = f8, f7, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f10, f7, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f10 = f9, f9, f8
; CHECK-NEXT:    mov f8 = f6 ;;
; CHECK-NEXT:    (p8) fma.s f8 = f10, f7, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

; A double takes the steps of Intel's sequence for the least latency, which
; finish the root from a polynomial in e = 1/2 - g * h rather than in more
; steps of Newton's method.

define double @sqrt_double(double %a) {
; CHECK-LABEL: sqrt_double:
; CHECK:         movl	r14 = 4602678819172646912
; CHECK-NEXT:    frsqrta	f7, p8 = f8 ;;
; CHECK:         setf.d	f6 = r14
; CHECK-NEXT:    (p8) fma.s1	f9 = f8, f7, f0 ;;
; CHECK:         (p8) fma.s1	f10 = f6, f7, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1	f11 = f9, f10, f6 ;;
; CHECK-NEXT:    (p8) fma.s1	f9 = f11, f9, f9
; CHECK-NEXT:    (p8) fma.s1	f10 = f11, f10, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1	f6 = f9, f10, f6 ;;
; CHECK-NEXT:    (p8) fma.s1	f9 = f6, f9, f9
; CHECK-NEXT:    (p8) fma.s1	f6 = f6, f10, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1	f10 = f9, f9, f8 ;;
; CHECK-NEXT:    (p8) fma.s1	f9 = f10, f6, f9 ;;
; CHECK-NEXT:    (p8) fnma.s1	f10 = f9, f9, f8
; CHECK-NEXT:    mov	f8 = f7 ;;
; CHECK-NEXT:    (p8) fma.d	f8 = f10, f6, f9
; CHECK-NEXT:    br.ret.sptk.many	b0 ;;
; LATENCY-LABEL: sqrt_double:
; LATENCY:         movl r14 = 4602678819172646912
; LATENCY-NEXT:    frsqrta f7, p8 = f8
; LATENCY-NEXT:    movl r16 = 4612811918334230528 ;;
; LATENCY-NEXT:    setf.d f6 = r14
; LATENCY-NEXT:    movl r15 = 4609434218613702656
; LATENCY-NEXT:    setf.d f11 = r16
; LATENCY-NEXT:    movl r17 = 4620552480193773568 ;;
; LATENCY-NEXT:    setf.d f12 = r15
; LATENCY-NEXT:    movl r18 = 4624317208007278592
; LATENCY-NEXT:    setf.d f13 = r17
; LATENCY-NEXT:    movl r19 = 4616611830519824384 ;;
; LATENCY-NEXT:    setf.d f14 = r18
; LATENCY-NEXT:    (p8) fma.s1 f9 = f8, f7, f0
; LATENCY-NEXT:    (p8) fma.s1 f10 = f6, f7, f0 ;;
; LATENCY-NEXT:    (p8) fnma.s1 f6 = f9, f10, f6 ;;
; LATENCY-NEXT:    (p8) fma.s1 f11 = f11, f6, f12
; LATENCY-NEXT:    setf.d f12 = r19
; LATENCY-NEXT:    (p8) fma.s1 f15 = f6, f6, f0
; LATENCY-NEXT:    (p8) fma.s1 f13 = f14, f6, f13 ;;
; LATENCY-NEXT:    (p8) fma.s1 f14 = f15, f15, f0
; LATENCY-NEXT:    (p8) fma.s1 f11 = f15, f11, f6
; LATENCY-NEXT:    (p8) fma.s1 f6 = f6, f13, f12 ;;
; LATENCY-NEXT:    (p8) fma.s1 f12 = f11, f9, f9
; LATENCY-NEXT:    (p8) fma.s1 f9 = f9, f14, f0
; LATENCY-NEXT:    (p8) fma.s1 f11 = f14, f6, f11 ;;
; LATENCY-NEXT:    (p8) fma.d.s1 f6 = f6, f9, f12
; LATENCY-NEXT:    (p8) fma.s1 f9 = f11, f10, f10 ;;
; LATENCY-NEXT:    (p8) fnma.s1 f10 = f6, f6, f8
; LATENCY-NEXT:    mov f8 = f7 ;;
; LATENCY-NEXT:    (p8) fma.d f8 = f10, f9, f6
; LATENCY-NEXT:    br.ret.sptk.many b0 ;;
  %r = call double @llvm.sqrt.f64(double %a)
  ret double %r
}

; A call to sqrt that may set errno takes the root inline, and calls only
; when the root is a NaN.

define double @libm_sqrt(double %a) {
; CHECK-LABEL: libm_sqrt:
; CHECK:         frsqrta
; CHECK:         (p8) fma.d f8 =
; CHECK:         fcmp.unord p6, p7 = f8, f8 ;;
; CHECK-NEXT:    (p6) br.cond.dpnt.few .LBB2_2
; CHECK:         br.call.sptk.many b0 = sqrt ;;
  %r = call double @sqrt(double %a)
  ret double %r
}

; A long double still calls the library.

define x86_fp80 @sqrt_long_double(x86_fp80 %a) {
; CHECK-LABEL: sqrt_long_double:
; CHECK-NOT:     frsqrta
; CHECK:         br.call.sptk.many b0 = sqrtl ;;
  %r = call x86_fp80 @llvm.sqrt.f80(x86_fp80 %a)
  ret x86_fp80 %r
}

declare double @sqrt(double)
