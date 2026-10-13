; RUN: llc -mtriple=x86_64-apple-macosx10.9 -mattr=+soft-float < %s | FileCheck %s
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu -mattr=+soft-float -filetype=null %s 2>&1 | FileCheck %s --check-prefix=ERR

; A softened sincospi is a call of the library routine where there is one, and
; an error where there is none; it used to stop the legalizer in either case.

; ERR: error: do not know how to soften fsincospi

define { double, double } @test_sincospi_f64(double %a) {
; CHECK-LABEL: test_sincospi_f64:
; CHECK:       leaq {{[0-9]*}}(%rsp), %rsi
; CHECK-NEXT:  leaq {{[0-9]*}}(%rsp), %rdx
; CHECK-NEXT:  callq ___sincospi
; CHECK-NEXT:  movq {{[0-9]*}}(%rsp), %rax
; CHECK-NEXT:  movq {{[0-9]*}}(%rsp), %rdx
; CHECK:       retq
  %result = call { double, double } @llvm.sincospi.f64(double %a)
  ret { double, double } %result
}

define { float, float } @test_sincospi_f32(float %a) {
; CHECK-LABEL: test_sincospi_f32:
; CHECK:       callq ___sincospif
; CHECK-NEXT:  movl {{[0-9]*}}(%rsp), %eax
; CHECK-NEXT:  movl {{[0-9]*}}(%rsp), %edx
; CHECK:       retq
  %result = call { float, float } @llvm.sincospi.f32(float %a)
  ret { float, float } %result
}
