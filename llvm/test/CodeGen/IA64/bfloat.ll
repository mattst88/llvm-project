; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; Nothing computes with a bfloat. It is kept as sixteen bits in a general
; register and made a float to be used.

; The upper half of the bits of a float, rounded to nearest and to even: what
; is just below a half is added to the lower half, and one more when the
; upper half is odd. A NaN is made a quiet one instead, since adding could
; make a number of it.
; CHECK-LABEL: from_float:
; CHECK:      getf.s r14 = f8
; CHECK-NEXT: mov r15 = 32767
; CHECK-NEXT: fcmp.unord p6, p7 = f8, f8 ;;
; CHECK:      extr.u r16 = r14, 16, 1
; CHECK-NEXT: add r15 = r14, r15
; CHECK-NEXT: dep r14 = -1, r14, 22, 1 ;;
; CHECK-NEXT: add r15 = r16, r15 ;;
; CHECK-NEXT: (p6) mov r15 = r14 ;;
; CHECK-NEXT: shr.u r8 = r15, 16
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i16 @from_float(float %a) {
  %r = fptrunc float %a to bfloat
  %b = bitcast bfloat %r to i16
  ret i16 %b
}

; A double is not rounded to a float and then again. Where the float is not
; the double, the odd one of the two floats the double is between is rounded
; in its place: one is taken from the bits unless the float is the nearer
; zero, and the lowest bit is set.
; CHECK-LABEL: from_double:
; CHECK:      fma.s f6 = f8, f1, f0
; CHECK:      fma.d f7 = f6, f1, f0
; CHECK:      fcmp.lt p6, p7 = f10, f9 ;;
; CHECK-NEXT: (p6) mov r15 = r0
; CHECK:      sub r15 = r14, r15
; CHECK:      or r15 = 1, r15
; CHECK:      shr.u r8 = r14, 16
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i16 @from_double(double %a) {
  %r = fptrunc double %a to bfloat
  %b = bitcast bfloat %r to i16
  ret i16 %b
}

; The other way the sixteen bits are the upper half of those of a float.
; CHECK-LABEL: to_float:
; CHECK:      shl r14 = r32, 16 ;;
; CHECK-NEXT: setf.s f8 = r14
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define float @to_float(i16 %a) {
  %b = bitcast i16 %a to bfloat
  %r = fpext bfloat %b to float
  ret float %r
}

; In memory it is sixteen bits, and it is computed with as a float.
; CHECK-LABEL: store:
; CHECK:      ld2 r14 = [r32]
; CHECK:      shl r14 = r14, 16 ;;
; CHECK-NEXT: setf.s f6 = r14 ;;
; CHECK-NEXT: fma.s f6 = f6, f1, f6 ;;
; CHECK-NEXT: getf.s r14 = f6
; CHECK:      shr.u r14 = r15, 16 ;;
; CHECK-NEXT: st2 [r33] = r14
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define void @store(ptr %p, ptr %q) {
  %a = load bfloat, ptr %p
  %r = fadd bfloat %a, %a
  store bfloat %r, ptr %q
  ret void
}
