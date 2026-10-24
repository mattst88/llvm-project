; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR

; Every instruction rounds as ar.fpsr says and raises what it raises, so an
; operation that is to do both at run time is the instruction it would be
; anyway. What differs is that it is kept, and kept in its place.

define double @add(double %a, double %b) strictfp {
; CHECK-LABEL: add:
; CHECK: fma.d f8 = f8, f1, f9
; MIR-LABEL: name: add
; MIR-NOT: nofpexcept
; MIR: FMA_D {{.*}}, implicit $ar40
  %r = call double @llvm.experimental.constrained.fadd.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define float @sub(float %a, float %b) strictfp {
; CHECK-LABEL: sub:
; CHECK: fms.s f8 = f8, f1, f9
  %r = call float @llvm.experimental.constrained.fsub.f32(float %a, float %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %r
}

define x86_fp80 @mul(x86_fp80 %a, x86_fp80 %b) strictfp {
; CHECK-LABEL: mul:
; CHECK: fma f8 = f8, f9, f0
  %r = call x86_fp80 @llvm.experimental.constrained.fmul.f80(x86_fp80 %a, x86_fp80 %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret x86_fp80 %r
}

define double @muladd(double %a, double %b, double %c) strictfp {
; CHECK-LABEL: muladd:
; CHECK: fma.d f8 = f8, f9, f10
  %r = call double @llvm.experimental.constrained.fma.f64(double %a, double %b, double %c, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

; An operation whose result is not used still raises its exceptions.

define void @unused(double %a, double %b) strictfp {
; CHECK-LABEL: unused:
; CHECK: fma.d {{f[0-9]+}} = f8, f9, f0
  %r = call double @llvm.experimental.constrained.fmul.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret void
}

; A quotient is finished in line, with the steps between the first and the
; last in a status field that keeps their flags to itself.

define double @div(double %a, double %b) strictfp {
; CHECK-LABEL: div:
; CHECK: frcpa [[Y:f[0-9]+]], [[P:p[0-9]+]] = f8, f9
; CHECK: ([[P]]) fma.s1
; CHECK: ([[P]]) fma.d {{f[0-9]+}} =
; CHECK-NOT: br.call
; MIR-LABEL: name: div
; MIR-NOT: nofpexcept
; MIR: FRCPA
; MIR-NOT: nofpexcept
; MIR: FMA_D_MERGE
  %r = call double @llvm.experimental.constrained.fdiv.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define x86_fp80 @div_extended(x86_fp80 %a, x86_fp80 %b) strictfp {
; CHECK-LABEL: div_extended:
; CHECK: br.call.sptk.many b0 = __divxf3
  %r = call x86_fp80 @llvm.experimental.constrained.fdiv.f80(x86_fp80 %a, x86_fp80 %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret x86_fp80 %r
}

define double @root(double %a) strictfp {
; CHECK-LABEL: root:
; CHECK: br.call.sptk.many b0 = sqrt
  %r = call double @llvm.experimental.constrained.sqrt.f64(double %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @to_integral(double %a) strictfp {
; CHECK-LABEL: to_integral:
; CHECK: br.call.sptk.many b0 = rint
  %r = call double @llvm.experimental.constrained.rint.f64(double %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define float @narrow(double %a) strictfp {
; CHECK-LABEL: narrow:
; CHECK: fma.s f8 = f8, f1, f0
  %r = call float @llvm.experimental.constrained.fptrunc.f32.f64(double %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %r
}

define double @widen(float %a) strictfp {
; CHECK-LABEL: widen:
; CHECK: fma.d f8 = f8, f1, f0
  %r = call double @llvm.experimental.constrained.fpext.f64.f32(float %a, metadata !"fpexcept.strict")
  ret double %r
}

define x86_fp80 @widen_extended(double %a) strictfp {
; CHECK-LABEL: widen_extended:
; CHECK: fma f8 = f8, f1, f0
  %r = call x86_fp80 @llvm.experimental.constrained.fpext.f80.f64(double %a, metadata !"fpexcept.strict")
  ret x86_fp80 %r
}

define i64 @to_signed(double %a) strictfp {
; CHECK-LABEL: to_signed:
; CHECK: fcvt.fx.trunc [[F:f[0-9]+]] = f8
; CHECK: getf.sig r8 = [[F]]
  %r = call i64 @llvm.experimental.constrained.fptosi.i64.f64(double %a, metadata !"fpexcept.strict")
  ret i64 %r
}

define i32 @to_unsigned(float %a) strictfp {
; CHECK-LABEL: to_unsigned:
; CHECK: fcvt.fxu.trunc [[F:f[0-9]+]] = f8
; CHECK: getf.sig r8 = [[F]]
  %r = call i32 @llvm.experimental.constrained.fptoui.i32.f32(float %a, metadata !"fpexcept.strict")
  ret i32 %r
}

define double @from_signed(i64 %a) strictfp {
; CHECK-LABEL: from_signed:
; CHECK: setf.sig [[F:f[0-9]+]] = r32
; CHECK: fcvt.xf [[G:f[0-9]+]] = [[F]]
; CHECK: fma.d f8 = [[G]], f1, f0
  %r = call double @llvm.experimental.constrained.sitofp.f64.i64(i64 %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define float @from_unsigned(i32 %a) strictfp {
; CHECK-LABEL: from_unsigned:
; CHECK: addp4 [[R:r[0-9]+]] = r32, r0
; CHECK: setf.sig [[F:f[0-9]+]] = [[R]]
; CHECK: fma.s f8 = [[F]], f1, f0
  %r = call float @llvm.experimental.constrained.uitofp.f32.i32(i32 %a, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %r
}

; fcmp.lt and fcmp.le raise the invalid-operation exception for operands
; that have no order, which is what a compare that signals is to do.

define i1 @less_signaling(double %a, double %b) strictfp {
; CHECK-LABEL: less_signaling:
; CHECK: fcmp.lt p6, p7 = f8, f9
; CHECK-NOT: fcmp
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"olt", metadata !"fpexcept.strict")
  ret i1 %r
}

; One that is quiet asks first whether there is an order, and which way it
; runs only if there is.

define i1 @less_quiet(double %a, double %b) strictfp {
; CHECK-LABEL: less_quiet:
; CHECK: fcmp.unord p7, p6 = f8, f9
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) fcmp.lt p6, p7 = f8, f9
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"olt", metadata !"fpexcept.strict")
  ret i1 %r
}

; With the operands exchanged for the compare but not for the question.

define i1 @greater_or_equal_quiet(float %a, float %b) strictfp {
; CHECK-LABEL: greater_or_equal_quiet:
; CHECK: fcmp.unord p7, p6 = f8, f9
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p6) fcmp.le p6, p7 = f9, f8
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"oge", metadata !"fpexcept.strict")
  ret i1 %r
}

; Where having no order makes the condition true, the predicates are the
; other way around.

define i1 @not_less_quiet(double %a, double %b) strictfp {
; CHECK-LABEL: not_less_quiet:
; CHECK: fcmp.unord p6, p7 = f8, f9
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: (p7) fcmp.lt p7, p6 = f8, f9
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"uge", metadata !"fpexcept.strict")
  ret i1 %r
}

; fcmp.eq and fcmp.unord are quiet.

define i1 @equal_quiet(double %a, double %b) strictfp {
; CHECK-LABEL: equal_quiet:
; CHECK: fcmp.eq p6, p7 = f8, f9
; CHECK-NOT: fcmp
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict")
  ret i1 %r
}

define i1 @unordered_quiet(x86_fp80 %a, x86_fp80 %b) strictfp {
; CHECK-LABEL: unordered_quiet:
; CHECK: fcmp.unord p6, p7 = f8, f9
; CHECK-NOT: fcmp
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmp.f80(x86_fp80 %a, x86_fp80 %b, metadata !"uno", metadata !"fpexcept.strict")
  ret i1 %r
}

; So one of them that is to signal has a compare before it that does.

define i1 @equal_signaling(double %a, double %b) strictfp {
; CHECK-LABEL: equal_signaling:
; CHECK: fcmp.lt p6, p7 = f8, f9
; CHECK: fcmp.eq p6, p7 = f8, f9
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict")
  ret i1 %r
}

define i1 @differ_signaling(double %a, double %b) strictfp {
; CHECK-LABEL: differ_signaling:
; CHECK: fcmp.lt p7, p6 = f8, f9
; CHECK: fcmp.eq p7, p6 = f8, f9
; CHECK: (p6) mov r8 = 1
  %r = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"une", metadata !"fpexcept.strict")
  ret i1 %r
}

; Two that are equal, or have no order, is not one compare.

define i1 @equal_or_unordered(double %a, double %b) strictfp {
; CHECK-LABEL: equal_or_unordered:
; CHECK-DAG: fcmp.eq
; CHECK-DAG: fcmp.unord
; CHECK: br.ret
  %r = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ueq", metadata !"fpexcept.strict")
  ret i1 %r
}

; A choice by a compare.

define double @choose(double %a, double %b) strictfp {
; CHECK-LABEL: choose:
; CHECK: fcmp.lt p6, p7 = f8, f9
; CHECK: br.ret
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"olt", metadata !"fpexcept.strict")
  %r = select i1 %c, double %a, double %b
  ret double %r
}

; Quadruple precision is the library's.

define fp128 @add_quad(fp128 %a, fp128 %b) strictfp {
; CHECK-LABEL: add_quad:
; CHECK: br.call.sptk.many b0 = __addtf3
  %r = call fp128 @llvm.experimental.constrained.fadd.f128(fp128 %a, fp128 %b, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret fp128 %r
}

define i1 @less_quad(fp128 %a, fp128 %b) strictfp {
; CHECK-LABEL: less_quad:
; CHECK: br.call.sptk.many b0 = __lttf2
  %r = call i1 @llvm.experimental.constrained.fcmps.f128(fp128 %a, fp128 %b, metadata !"olt", metadata !"fpexcept.strict")
  ret i1 %r
}
