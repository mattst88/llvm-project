; RUN: llc -mtriple=ia64-unknown-linux-gnu -ia64-fp-anti-dep-breaking=0 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; There is no instruction that divides. frcpa gives the reciprocal of the
; divisor to under nine bits, and a predicate that is clear when an operand is
; a zero, an infinity or a NaN and what it gave is the quotient. Each step from
; there doubles the bits that are right, in status field 1 so that no flag of
; a step is one the program sees, and the last adds what the remainder says is
; missing and rounds once, as the program asked. A quotient that is returned
; is written to f8 by that last step, which leaves in place a copy of what
; frcpa gave, made once the dividend in f8 is dead.

define float @div_float(float %a, float %b) {
; CHECK-LABEL: div_float:
; CHECK:         frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f9, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f7, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f10, f6 ;;
; CHECK-NEXT:    fma.s.s1 f10 = f8, f7, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f9, f10, f8
; CHECK-NEXT:    mov f8 = f6 ;;
; CHECK-NEXT:    (p8) fma.s f8 = f9, f7, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv float %a, %b
  ret float %r
}

; A double has more than twice the bits, and takes one more step.

define double @div_double(double %a, double %b) {
; CHECK-LABEL: div_double:
; CHECK:         frcpa f6, p8 = f8, f9 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f9, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f7, f6, f6
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f7, f10, f10
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f10, f10 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f8, f7, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f9, f10, f8
; CHECK-NEXT:    mov f8 = f6 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f9, f7, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, %b
  ret double %r
}

; When the dividend is 1, q = a * y is y, and the steps that would make it
; again from y's are left out until the one that rounds it.

define double @recip_double(double %b) {
; CHECK-LABEL: recip_double:
; CHECK:         frcpa f6, p8 = f1, f8 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f8, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f7, f7, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f9, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f9, f7, f7 ;;
; CHECK-NEXT:    (p8) fma.d.s1 f9 = f10, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f7 = f10, f7, f7 ;;
; CHECK-NEXT:    (p8) fnma.s1 f10 = f8, f9, f1
; CHECK-NEXT:    mov f8 = f6 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f10, f7, f9
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double 1.0, %b
  ret double %r
}

define float @recip_float(float %b) {
; CHECK-LABEL: recip_float:
; CHECK:         frcpa f6, p8 = f1, f8 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f8, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f7, f7, f0
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f6, f6 ;;
; CHECK-NEXT:    (p8) fma.s.s1 f10 = f9, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f7 = f9, f7, f7 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f8, f10, f1
; CHECK-NEXT:    mov f8 = f6 ;;
; CHECK-NEXT:    (p8) fma.s f8 = f9, f7, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv float 1.0, %b
  ret float %r
}

; The predicate frcpa writes is not the one a comparison writes, so the
; comparison need not wait for the quotient that the choice it makes is of.

define double @div_select(double %a, double %b, double %x) {
; CHECK-LABEL: div_select:
; CHECK:         frcpa f6, p8 = f8, f9
; CHECK-NEXT:    fcmp.lt p6, p7 = f8, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f9, f6, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f11 = f7, f6, f6
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f11 = f7, f11, f11
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f11, f11 ;;
; CHECK-NEXT:    fma.d.s1 f11 = f8, f7, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f9, f11, f8 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f9, f7, f11 ;;
; CHECK-NEXT:    (p6) mov f10 = f6 ;;
; CHECK-NEXT:    mov f8 = f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %c = fcmp olt double %a, %x
  %d = fdiv double %a, %b
  %r = select i1 %c, double %d, double %x
  ret double %r
}

; Each division has a predicate of its own, so the steps of two divisions that
; do not depend on each other are in the same groups.

define double @two_div(double %a, double %b, double %c, double %d) {
; CHECK-LABEL: two_div:
; CHECK:         frcpa f6, p8 = f8, f9
; CHECK-NEXT:    frcpa f7, p9 = f10, f11 ;;
; CHECK-NEXT:    (p8) fnma.s1 f12 = f9, f6, f1
; CHECK-NEXT:    (p9) fnma.s1 f13 = f11, f7, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f14 = f12, f6, f6
; CHECK-NEXT:    (p8) fma.s1 f12 = f12, f12, f0
; CHECK-NEXT:    (p9) fma.s1 f15 = f13, f7, f7
; CHECK-NEXT:    (p9) fma.s1 f13 = f13, f13, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f14 = f12, f14, f14
; CHECK-NEXT:    (p8) fma.s1 f12 = f12, f12, f0
; CHECK-NEXT:    (p9) fma.s1 f15 = f13, f15, f15
; CHECK-NEXT:    (p9) fma.s1 f13 = f13, f13, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f12 = f12, f14, f14
; CHECK-NEXT:    (p9) fma.s1 f13 = f13, f15, f15 ;;
; CHECK-NEXT:    fma.d.s1 f14 = f8, f12, f0
; CHECK-NEXT:    fma.d.s1 f15 = f10, f13, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f8 = f9, f14, f8
; CHECK-NEXT:    (p9) fnma.s1 f9 = f11, f15, f10 ;;
; CHECK-NEXT:    (p8) fma.d f6 = f8, f12, f14
; CHECK-NEXT:    (p9) fma.d f7 = f9, f13, f15 ;;
; CHECK-NEXT:    fma.d f8 = f6, f1, f7
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %x = fdiv double %a, %b
  %y = fdiv double %c, %d
  %s = fadd double %x, %y
  ret double %s
}

; A dividend computed well after the divisor waits only for the last three
; steps: y is refined from frcpa of 1 and the divisor, under that frcpa's
; predicate, while the dividend is computed, and frcpa of the two runs beside
; q = a * y for the predicate of the other two.

define double @late_dividend(double %a, double %b) {
; CHECK-LABEL: late_dividend:
; CHECK:         frcpa.s1 f6, p8 = f1, f9
; CHECK-NEXT:    fma.d f10 = f8, f8, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f9, f6, f1
; CHECK-NEXT:    fma.d f10 = f10, f8, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f6 = f7, f6, f6
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0
; CHECK-NEXT:    fma.d f8 = f10, f8, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f6 = f7, f6, f6
; CHECK-NEXT:    (p8) fma.s1 f7 = f7, f7, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f6 = f7, f6, f6
; CHECK-NEXT:    frcpa f7, p8 = f8, f9 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f8, f6, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f9, f10, f8
; CHECK-NEXT:    mov f8 = f7 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f9, f6, f10
  %x = fmul double %a, %a
  %y = fmul double %x, %a
  %z = fmul double %y, %a
  %q = fdiv double %z, %b
  ret double %q
}

define float @late_dividend_float(float %a, float %b) {
; CHECK-LABEL: late_dividend_float:
; CHECK:         frcpa.s1 f6, p8 = f1, f9
; CHECK-NEXT:    fma.s f7 = f8, f8, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f10 = f9, f6, f1
; CHECK-NEXT:    fma.s f7 = f7, f8, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f11 = f10, f6, f6
; CHECK-NEXT:    fma.s f7 = f7, f8, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f6 = f10, f11, f6
; CHECK-NEXT:    frcpa f8, p8 = f7, f9 ;;
; CHECK-NEXT:    fma.s.s1 f10 = f7, f6, f0
; CHECK:         (p8) fnma.s1 f7 = f9, f10, f7 ;;
; CHECK-NEXT:    (p8) fma.s f8 = f7, f6, f10
  %x = fmul float %a, %a
  %y = fmul float %x, %a
  %z = fmul float %y, %a
  %q = fdiv float %z, %b
  ret float %q
}

; A quotient that is a product is one: the reciprocal of a power of two is
; exact.

define double @div_power_of_two(double %a) {
; CHECK-LABEL: div_power_of_two:
; CHECK-NOT:     frcpa
; CHECK:         fma.d f8 = f8, f6, f0
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 4.0
  ret double %r
}

; With a constant divisor b, y = 1 / b rounded to double gives q = a * y, and
; q + (a - b * q) * y rounds to a / b: a chain of three steps, which need not
; wait for frcpa. frcpa still divides zeros, infinities and NaNs.

define double @div_const(double %a) {
; CHECK-LABEL: div_const:
; CHECK:         movl r14 = 4592670820000712476
; CHECK-NEXT:    movl r15 = 4621256167635550208 ;;
; CHECK-NEXT:    setf.d f6 = r14
; CHECK-NEXT:    setf.d f7 = r15 ;;
; CHECK-NEXT:    fma.s1 f10 = f8, f6, f0
; CHECK-NEXT:    frcpa f9, p8 = f8, f7 ;;
; CHECK-NEXT:    fnma.s1 f7 = f7, f10, f8
; CHECK-NEXT:    mov f8 = f9 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f7, f6, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 9.0
  ret double %r
}

define float @div_const_float(float %a) {
; CHECK-LABEL: div_const_float:
; CHECK:         movl r14 = 4599676419421066581
; CHECK-NEXT:    movl r15 = 1077936128 ;;
; CHECK-NEXT:    setf.d f6 = r14
; CHECK-NEXT:    setf.s f7 = r15 ;;
; CHECK-NEXT:    fma.s1 f10 = f8, f6, f0
; CHECK-NEXT:    frcpa f9, p8 = f8, f7 ;;
; CHECK-NEXT:    fnma.s1 f7 = f7, f10, f8
; CHECK-NEXT:    mov f8 = f9 ;;
; CHECK-NEXT:    (p8) fma.s f8 = f7, f6, f10
  %r = fdiv float %a, 3.0
  ret float %r
}

; -1.5 is -3 * 2^-1.

define double @div_const_fraction(double %a) {
; CHECK-LABEL: div_const_fraction:
; CHECK:         movl r14 = -4619192017806338731
; CHECK-NEXT:    movl r15 = -4613937818241073152 ;;
; CHECK:         fnma.s1 f7 = f7, f10, f8
; CHECK-NEXT:    mov f8 = f9 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f7, f6, f10
  %r = fdiv double %a, -1.5
  ret double %r
}

; A subnormal quotient by a multiple of 2 can be a midpoint, as 5 * 2^-1074
; divided by 10 is, and only the full sequence rounds that to even.

define double @div_const_even(double %a) {
; CHECK-LABEL: div_const_even:
; CHECK:         movl r14 = 4621819117588971520 ;;
; CHECK-NEXT:    setf.d f6 = r14 ;;
; CHECK-NEXT:    frcpa f7, p8 = f8, f6 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f6, f7, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f10, f10
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f10, f10 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f8, f9, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f6 = f6, f10, f8
; CHECK-NEXT:    mov f8 = f7 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f6, f9, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 10.0
  ret double %r
}

; Nor is the sum close enough to a / b to be sure of rounding right when b's
; significand has more than 32 bits, as that of 1/3 does.

define double @div_const_long(double %a) {
; CHECK-LABEL: div_const_long:
; CHECK:         movl r14 = 4599676419421066581 ;;
; CHECK-NEXT:    setf.d f6 = r14 ;;
; CHECK-NEXT:    frcpa f7, p8 = f8, f6 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f6, f7, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f10, f10
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f10, f10 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f8, f9, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f6 = f6, f10, f8
; CHECK-NEXT:    mov f8 = f7 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f6, f9, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = fdiv double %a, 0x3FD5555555555555
  ret double %r
}

; Strict division keeps the full sequence, which rounds in any mode.

define double @div_const_strict(double %a) strictfp {
; CHECK-LABEL: div_const_strict:
; CHECK:         movl r14 = 4621256167635550208 ;;
; CHECK-NEXT:    setf.d f6 = r14 ;;
; CHECK-NEXT:    frcpa f7, p8 = f8, f6 ;;
; CHECK-NEXT:    (p8) fnma.s1 f9 = f6, f7, f1 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f7, f7
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f10 = f9, f10, f10
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f9, f0 ;;
; CHECK-NEXT:    (p8) fma.s1 f9 = f9, f10, f10 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f8, f9, f0 ;;
; CHECK-NEXT:    (p8) fnma.s1 f6 = f6, f10, f8
; CHECK-NEXT:    mov f8 = f7 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f6, f9, f10
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = call double @llvm.experimental.constrained.fdiv.f64(double %a, double 9.0, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ret double %r
}

; A division by a square root starts from the square root's own reciprocal.
; The sequence for sqrt(x) leaves h ~= 0.5/sqrt(x), refined to 1 ulp, so 2h
; seeds Newton's method two steps nearer than frcpa's nine bits. frcpa still
; sets the predicate, and its approximation is the result when it is clear.

define double @div_sqrt(double %a, double %x) {
; CHECK-LABEL: div_sqrt:
; CHECK:         fma.s1 f6 = f10, f1, f10 ;;
; CHECK-NEXT:    fnma.s1 f9 = f7, f6, f1
; CHECK-NEXT:    fma.s1 f10 = f8, f6, f0
; CHECK-NEXT:    frcpa f11, p8 = f8, f7 ;;
; CHECK-NEXT:    fma.d.s1 f10 = f9, f10, f10
; CHECK-NEXT:    fma.s1 f6 = f9, f6, f6 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f7, f10, f8
; CHECK-NEXT:    mov f8 = f11 ;;
; CHECK-NEXT:    (p8) fma.d f8 = f7, f6, f10
  %s = call double @llvm.sqrt.f64(double %x)
  %q = fdiv double %a, %s
  ret double %q
}

; So does one by a sqrt that may set errno, which is inline but for a call
; when the result is NaN. That result reaches the division, whose predicate
; is then clear.

define double @div_sqrt_errno(double %a, double %x) {
; CHECK-LABEL: div_sqrt_errno:
; CHECK:         frcpa f11, p8 = f8, f6
; CHECK:         fma.s1 f7 = f7, f1, f7
; CHECK:         (p8) fma.d f8 = f6, f7, f10
  %s = call double @sqrt(double %x)
  %q = fdiv double %a, %s
  ret double %q
}

; A divisor that is only sometimes the square root keeps the usual sequence.

define double @div_sqrt_or_other(double %a, double %x, double %y, i1 %p) {
; CHECK-LABEL: div_sqrt_or_other:
; CHECK:         frcpa f6, p8 = f8, f10 ;;
; CHECK-NEXT:    (p8) fnma.s1 f7 = f10, f6, f1 ;;
entry:
  %s = call double @llvm.sqrt.f64(double %x)
  br i1 %p, label %split, label %other

other:
  br label %split

split:
  %b = phi double [ %s, %entry ], [ %y, %other ]
  %q = fdiv double %a, %b
  ret double %q
}

declare double @sqrt(double)

; A divisor from another block can have y refined outside a loop that the
; dividend is computed in. The quotient is not predicated, so that the loop
; can still rotate.

define void @div_invariant(ptr %o, ptr %p, i64 %n, double %h) {
; CHECK-LABEL: div_invariant:
; CHECK:         frcpa.s1 {{f[0-9]+}}, {{p[0-9]+}} = f1, f8
; CHECK:       .LBB{{[0-9_]+}}:
; CHECK-NOT:     frcpa.s1
; CHECK:         br.cloop
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %a = getelementptr double, ptr %p, i64 %i
  %v = load double, ptr %a
  %d = fdiv double %v, %h
  %b = getelementptr double, ptr %o, i64 %i
  store double %d, ptr %b
  %i1 = add i64 %i, 1
  %c = icmp ne i64 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
