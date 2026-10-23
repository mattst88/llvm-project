; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; long double is the 80-bit extended format, which is what a register holds.
; It is sixteen bytes in memory and two slots as an argument.

define x86_fp80 @add(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: add:
; CHECK:       fma f8 = f8, f1, f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = fadd x86_fp80 %a, %b
  ret x86_fp80 %r
}

define x86_fp80 @sub(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: sub:
; CHECK:       fms f8 = f8, f1, f9
  %r = fsub x86_fp80 %a, %b
  ret x86_fp80 %r
}

define x86_fp80 @mul(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: mul:
; CHECK:       fma f8 = f8, f9, f0
  %r = fmul x86_fp80 %a, %b
  ret x86_fp80 %r
}

define x86_fp80 @muladd(x86_fp80 %a, x86_fp80 %b, x86_fp80 %c) {
; CHECK-LABEL: muladd:
; CHECK:       fma f8 = f8, f9, f10
  %r = call x86_fp80 @llvm.fmuladd.f80(x86_fp80 %a, x86_fp80 %b, x86_fp80 %c)
  ret x86_fp80 %r
}

define x86_fp80 @neg(x86_fp80 %a) {
; CHECK-LABEL: neg:
; CHECK:       fmerge.ns f8 = f8, f8
  %r = fneg x86_fp80 %a
  ret x86_fp80 %r
}

define x86_fp80 @abs(x86_fp80 %a) {
; CHECK-LABEL: abs:
; CHECK:       fmerge.s f8 = f0, f8
  %r = call x86_fp80 @llvm.fabs.f80(x86_fp80 %a)
  ret x86_fp80 %r
}

define x86_fp80 @copysign(x86_fp80 %a, double %b) {
; CHECK-LABEL: copysign:
; CHECK:       fmerge.s f8 = f9, f8
  %s = fpext double %b to x86_fp80
  %r = call x86_fp80 @llvm.copysign.f80(x86_fp80 %a, x86_fp80 %s)
  ret x86_fp80 %r
}

; There is no instruction that divides or takes a square root.
define x86_fp80 @div(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: div:
; CHECK:       br.call.sptk.many b0 = __divxf3 ;;
  %r = fdiv x86_fp80 %a, %b
  ret x86_fp80 %r
}

define x86_fp80 @sqrt(x86_fp80 %a) {
; CHECK-LABEL: sqrt:
; CHECK:       br.call.sptk.many b0 = sqrtl ;;
  %r = call x86_fp80 @llvm.sqrt.f80(x86_fp80 %a)
  ret x86_fp80 %r
}

define i64 @lrint(x86_fp80 %a) {
; CHECK-LABEL: lrint:
; CHECK:       br.call.sptk.many b0 = lrintl ;;
  %r = call i64 @llvm.lrint.i64.f80(x86_fp80 %a)
  ret i64 %r
}

; A narrower value is made a normal number as it is widened, since the
; significand is stored as it is.
define x86_fp80 @extend(double %a) {
; CHECK-LABEL: extend:
; CHECK:       fma f8 = f8, f1, f0
  %r = fpext double %a to x86_fp80
  ret x86_fp80 %r
}

define x86_fp80 @extend_load(ptr %p) {
; CHECK-LABEL: extend_load:
; CHECK:       ldfs f6 = [r32] ;;
; CHECK-NEXT:  fma f8 = f6, f1, f0
  %a = load float, ptr %p
  %r = fpext float %a to x86_fp80
  ret x86_fp80 %r
}

define double @round(x86_fp80 %a) {
; CHECK-LABEL: round:
; CHECK:       fma.d f8 = f8, f1, f0
  %r = fptrunc x86_fp80 %a to double
  ret double %r
}

define void @round_store(ptr %p, x86_fp80 %a) {
; CHECK-LABEL: round_store:
; CHECK:       fma.s f6 = f8, f1, f0 ;;
; CHECK-NEXT:  stfs [r32] = f6
  %r = fptrunc x86_fp80 %a to float
  store float %r, ptr %p
  ret void
}

define i64 @to_signed(x86_fp80 %a) {
; CHECK-LABEL: to_signed:
; CHECK:       fcvt.fx.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %r = fptosi x86_fp80 %a to i64
  ret i64 %r
}

define i64 @to_unsigned(x86_fp80 %a) {
; CHECK-LABEL: to_unsigned:
; CHECK:       fcvt.fxu.trunc f6 = f8 ;;
; CHECK-NEXT:  getf.sig r8 = f6
  %r = fptoui x86_fp80 %a to i64
  ret i64 %r
}

define x86_fp80 @from_signed(i64 %a) {
; CHECK-LABEL: from_signed:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fcvt.xf f8 = f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sitofp i64 %a to x86_fp80
  ret x86_fp80 %r
}

define x86_fp80 @from_unsigned(i64 %a) {
; CHECK-LABEL: from_unsigned:
; CHECK:       setf.sig f6 = r32 ;;
; CHECK-NEXT:  fma f8 = f6, f1, f0
  %r = uitofp i64 %a to x86_fp80
  ret x86_fp80 %r
}

define i128 @to_wide(x86_fp80 %a) {
; CHECK-LABEL: to_wide:
; CHECK:       br.call.sptk.many b0 = __fixxfti ;;
  %r = fptosi x86_fp80 %a to i128
  ret i128 %r
}

define i1 @less(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: less:
; CHECK:       fcmp.lt p6, p7 = f8, f9
  %r = fcmp olt x86_fp80 %a, %b
  ret i1 %r
}

define x86_fp80 @select(i64 %c, x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: select:
; CHECK:       cmp.eq p6, p7 = 0, r32 ;;
; CHECK-NEXT:  (p6) mov f8 = f9
  %z = icmp eq i64 %c, 0
  %r = select i1 %z, x86_fp80 %b, x86_fp80 %a
  ret x86_fp80 %r
}

define x86_fp80 @load(ptr %p) {
; CHECK-LABEL: load:
; CHECK:       ldfe f8 = [r32]
  %r = load x86_fp80, ptr %p
  ret x86_fp80 %r
}

define void @store(ptr %p, x86_fp80 %a) {
; CHECK-LABEL: store:
; CHECK:       adds r14 = 16, r32 ;;
; CHECK-NEXT:  stfe [r14] = f8
  %q = getelementptr x86_fp80, ptr %p, i64 1
  store x86_fp80 %a, ptr %q
  ret void
}

; f0 and f1 are 0.0 and 1.0 at any precision, and any other constant has
; more bits than a general register.
define x86_fp80 @zero() {
; CHECK-LABEL: zero:
; CHECK:       mov f8 = f0
  ret x86_fp80 0xK00000000000000000000
}

define x86_fp80 @one() {
; CHECK-LABEL: one:
; CHECK:       mov f8 = f1
  ret x86_fp80 0xK3FFF8000000000000000
}

; CHECK:       .section .rodata.cst16,"aM",@progbits,16
; CHECK:       data8.ua 0xcccccccccccccccd
; CHECK-NEXT:  data2.ua 0x3ffb
define x86_fp80 @tenth() {
; CHECK-LABEL: tenth:
; CHECK:       movl r14 = @gprel(.LCPI25_0) ;;
; CHECK-NEXT:  add r14 = r14, r1 ;;
; CHECK-NEXT:  ldfe f8 = [r14]
  ret x86_fp80 0xK3FFBCCCCCCCCCCCCCCCD
}

; Each is two slots and one floating-point register.
declare x86_fp80 @g(x86_fp80, i32, x86_fp80)
define x86_fp80 @call(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: call:
; CHECK:       mov f6 = f8
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  mov r37 = 1
; CHECK-NEXT:  mov f8 = f9 ;;
; CHECK-NEXT:  mov f9 = f6
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
  %r = call x86_fp80 @g(x86_fp80 %b, i32 1, x86_fp80 %a)
  ret x86_fp80 %r
}

; The slot after an odd number of them is left out.
define i64 @after_odd(i64 %a, x86_fp80 %b, i64 %c) {
; CHECK-LABEL: after_odd:
; CHECK:       mov r8 = r36
  ret i64 %c
}

; The fifth is in memory.
define x86_fp80 @fifth(x86_fp80 %a, x86_fp80 %b, x86_fp80 %c, x86_fp80 %d, x86_fp80 %e) {
; CHECK-LABEL: fifth:
; CHECK:       adds r14 = 16, r12 ;;
; CHECK-NEXT:  ldfe f8 = [r14]
  ret x86_fp80 %e
}

; One that the callee has not named is its bits in the two slots.
declare void @v(ptr, ...)
define void @call_variadic(ptr %f, x86_fp80 %a) {
; CHECK-LABEL: call_variadic:
; CHECK:       adds r14 = 48, r12
; CHECK-NEXT:  adds r15 = 32, r12
; CHECK-NEXT:  adds r18 = 16, r12
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  mov r36 = r32
; CHECK-NEXT:  mov r40 = 7 ;;
; CHECK-NEXT:  stfe [r14] = f8
; CHECK-NEXT:  stfe [r15] = f8
; CHECK-NEXT:  or r16 = 8, r14
; CHECK-NEXT:  or r17 = 8, r15 ;;
; CHECK-NEXT:  ld8 r39 = [r16]
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  ld8 r38 = [r14]
; CHECK-NEXT:  ld8 r43 = [r17]
; CHECK-NEXT:  ld8 r42 = [r15]
; CHECK-NEXT:  stfe [r18] = f8
; CHECK-NEXT:  br.call.sptk.many b0 = v ;;
  call void (ptr, ...) @v(ptr %f, x86_fp80 %a, i64 7, x86_fp80 %a, x86_fp80 %a)
  ret void
}

define x86_fp80 @next(ptr %ap) {
; CHECK-LABEL: next:
; CHECK:       ld8 r14 = [r32] ;;
; CHECK-NEXT:  adds r14 = 15, r14 ;;
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK-NEXT:  adds r15 = 16, r14 ;;
; CHECK-NEXT:  st8 [r32] = r15
; CHECK-NEXT:  ldfe f8 = [r14]
  %r = va_arg ptr %ap, x86_fp80
  ret x86_fp80 %r
}
