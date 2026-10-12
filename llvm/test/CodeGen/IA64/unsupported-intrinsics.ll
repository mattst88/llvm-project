; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %s -o /dev/null 2>&1 | FileCheck %s

; What there is no code for is an error that names the function, and the
; compiler goes on to the next one.

; The return link is in b0, which has no address.
; CHECK: error: {{.*}}in function return_slot ptr (): llvm.addressofreturnaddress is not supported on IA-64
define ptr @return_slot() {
  %r = call ptr @llvm.addressofreturnaddress.p0()
  ret ptr %r
}

; CHECK: error: {{.*}}in function set_jump i32 (ptr): llvm.eh.sjlj.setjmp is not supported on IA-64
define i32 @set_jump(ptr %buf) {
  %r = call i32 @llvm.eh.sjlj.setjmp(ptr %buf)
  ret i32 %r
}

; CHECK: error: {{.*}}in function long_jump void (ptr): llvm.eh.sjlj.longjmp is not supported on IA-64
define void @long_jump(ptr %buf) {
  call void @llvm.eh.sjlj.longjmp(ptr %buf)
  unreachable
}

; CHECK: error: {{.*}}in function round_to_single float (double): llvm.fptrunc.round is not supported on IA-64
define float @round_to_single(double %a) {
  %r = call float @llvm.fptrunc.round.f32.f64(double %a, metadata !"round.towardzero")
  ret float %r
}

; CHECK: error: {{.*}}in function round_to_double double (x86_fp80): llvm.fptrunc.round is not supported on IA-64
define double @round_to_double(x86_fp80 %a) {
  %r = call double @llvm.fptrunc.round.f64.f80(x86_fp80 %a, metadata !"round.upward")
  ret double %r
}

; The result is of a type that is not legal.
; CHECK: error: {{.*}}in function round_to_half half (double): llvm.fptrunc.round is not supported on IA-64
define half @round_to_half(double %a) {
  %r = call half @llvm.fptrunc.round.f16.f64(double %a, metadata !"round.tonearest")
  ret half %r
}

; CHECK: error: {{.*}}in function eh_return void (i64, ptr): llvm.eh.return is not supported on IA-64
define void @eh_return(i64 %offset, ptr %handler) {
  call void @llvm.eh.return.i64(i64 %offset, ptr %handler)
  unreachable
}
