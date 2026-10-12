; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; No instruction divides integers.

define i64 @sdiv(i64 %a, i64 %b) {
; CHECK-LABEL: sdiv:
; CHECK: br.call.sptk.many b0 = __divdi3 ;;
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @udiv(i64 %a, i64 %b) {
; CHECK-LABEL: udiv:
; CHECK: br.call.sptk.many b0 = __udivdi3 ;;
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @srem(i64 %a, i64 %b) {
; CHECK-LABEL: srem:
; CHECK: br.call.sptk.many b0 = __moddi3 ;;
  %r = srem i64 %a, %b
  ret i64 %r
}

; The operands are extended to the register first.
define i32 @urem32(i32 %a, i32 %b) {
; CHECK-LABEL: urem32:
; CHECK:      zxt4 r37 = r32
; CHECK-NEXT: zxt4 r38 = r33
; CHECK-NEXT: mov r34 = r1 ;;
; CHECK-NEXT: br.call.sptk.many b0 = __umoddi3 ;;
  %r = urem i32 %a, %b
  ret i32 %r
}
