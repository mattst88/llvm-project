; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; No register holds a quadruple-precision value. It is its bits in two
; general registers, aligned to sixteen bytes as an argument and so starting
; at an even slot, and coming back in r8 and r9. The library does everything
; that is done to one.

define fp128 @add(i32 %x, fp128 %a, fp128 %b) {
; CHECK-LABEL: add:
; CHECK:         mov r43 = r37
; CHECK-NEXT:    mov r42 = r36
; CHECK-NEXT:    mov r41 = r35
; CHECK-NEXT:    mov r40 = r34 ;;
; CHECK-NEXT:    br.call.sptk.many b0 = __addtf3 ;;
; CHECK-NOT:     r8
; CHECK:         br.ret.sptk.many b0 ;;
  %r = fadd fp128 %a, %b
  ret fp128 %r
}

define i32 @less(fp128 %a, fp128 %b) {
; CHECK-LABEL: less:
; CHECK:         br.call.sptk.many b0 = __lttf2 ;;
; CHECK:         sxt4 r14 = r8 ;;
; CHECK-NEXT:    cmp.lt p7, p6 = -1, r14
  %c = fcmp olt fp128 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define fp128 @from_double(double %d) {
; CHECK-LABEL: from_double:
; CHECK:         br.call.sptk.many b0 = __extenddftf2 ;;
  %r = fpext double %d to fp128
  ret fp128 %r
}

define fp128 @from_extended(x86_fp80 %d) {
; CHECK-LABEL: from_extended:
; CHECK:         br.call.sptk.many b0 = __extendxftf2 ;;
  %r = fpext x86_fp80 %d to fp128
  ret fp128 %r
}

define x86_fp80 @to_extended(fp128 %a) {
; CHECK-LABEL: to_extended:
; CHECK:         br.call.sptk.many b0 = __trunctfxf2 ;;
  %r = fptrunc fp128 %a to x86_fp80
  ret x86_fp80 %r
}

define i64 @to_integer(fp128 %a) {
; CHECK-LABEL: to_integer:
; CHECK:         mov r38 = r33
; CHECK-NEXT:    mov r37 = r32 ;;
; CHECK-NEXT:    br.call.sptk.many b0 = __fixtfdi ;;
  %r = fptosi fp128 %a to i64
  ret i64 %r
}

define fp128 @from_integer(i64 %a) {
; CHECK-LABEL: from_integer:
; CHECK:         br.call.sptk.many b0 = __floatditf ;;
  %r = sitofp i64 %a to fp128
  ret fp128 %r
}

; The functions of the mathematical library are named for the type, since
; long double is another one.

define fp128 @round_to_integer(fp128 %a) {
; CHECK-LABEL: round_to_integer:
; CHECK:         br.call.sptk.many b0 = rintf128 ;;
  %r = call fp128 @llvm.rint.f128(fp128 %a)
  ret fp128 %r
}

define fp128 @round_down(fp128 %a) {
; CHECK-LABEL: round_down:
; CHECK:         br.call.sptk.many b0 = floorf128 ;;
  %r = call fp128 @llvm.floor.f128(fp128 %a)
  ret fp128 %r
}

define fp128 @lesser(fp128 %a, fp128 %b) {
; CHECK-LABEL: lesser:
; CHECK:         br.call.sptk.many b0 = fminf128 ;;
  %r = call fp128 @llvm.minnum.f128(fp128 %a, fp128 %b)
  ret fp128 %r
}

define fp128 @root(fp128 %a) {
; CHECK-LABEL: root:
; CHECK:         br.call.sptk.many b0 = sqrtf128 ;;
  %r = call fp128 @llvm.sqrt.f128(fp128 %a)
  ret fp128 %r
}

define fp128 @strict_round_to_integer(fp128 %a) strictfp {
; CHECK-LABEL: strict_round_to_integer:
; CHECK:         br.call.sptk.many b0 = rintf128 ;;
  %r = call fp128 @llvm.experimental.constrained.rint.f128(fp128 %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret fp128 %r
}
