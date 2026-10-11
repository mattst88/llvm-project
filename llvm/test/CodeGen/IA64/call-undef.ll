; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An argument with no value gives its register none, and the call still
; reads the register, which the verifier lets it do only if the call says
; that the value is undefined.

declare i64 @callee(i64, i64)

define i64 @caller(i64 %a) {
; CHECK-LABEL: caller:
; CHECK:       br.call.sptk.many b0 = callee
  %r = call i64 @callee(i64 %a, i64 undef)
  ret i64 %r
}

; The entry point and the global pointer of a callee with no value are one
; load, and the register is still read for the global pointer after the
; entry point has been moved to b6.

define void @callee_undef() {
; CHECK-LABEL: callee_undef:
; CHECK:       mov b6 = r14
; CHECK-NEXT:  mov r1 = r14
; CHECK-NEXT:  br.call.sptk.many b0 = b6
  call void undef()
  ret void
}
