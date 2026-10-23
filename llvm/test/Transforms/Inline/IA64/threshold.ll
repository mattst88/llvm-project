; RUN: opt < %s -mtriple=ia64-unknown-linux-gnu -passes=inline \
; RUN:   -pass-remarks=inline -disable-output 2>&1 | FileCheck %s

; Calls are inlined up to twice the usual cost: this one's threshold, with
; the bonus for a callee of one block, is 337 on a target without a
; multiplier.
; CHECK: 'callee' inlined into 'caller' with (cost={{-?[0-9]+}}, threshold=675)

define internal i64 @callee(i64 %x) {
  %a = mul i64 %x, %x
  ret i64 %a
}

define i64 @caller(i64 %x) {
  %r = call i64 @callee(i64 %x)
  %s = call i64 @other(i64 %r)
  ret i64 %s
}

declare i64 @other(i64)
