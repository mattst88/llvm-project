; RUN: opt < %s -passes="print<cost-model>" -cost-kind=throughput \
; RUN:   -disable-output -mtriple=ia64-unknown-linux-gnu 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SPEED
; RUN: opt < %s -passes="print<cost-model>" -cost-kind=code-size \
; RUN:   -disable-output -mtriple=ia64-unknown-linux-gnu 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SIZE
; RUN: opt < %s -passes="print<cost-model>" -cost-kind=latency \
; RUN:   -disable-output -mtriple=ia64-unknown-linux-gnu 2>&1 \
; RUN:   | FileCheck %s --check-prefix=LATENCY

; A multiplication of integers moves its operands to the floating-point
; registers and its product back.

define void @multiply(i64 %a, i64 %b, i32 %c, i32 %d) {
; SPEED-LABEL: 'multiply'
; SPEED:       cost of 4 for instruction: %x = mul i64 %a, %b
; SPEED:       cost of 4 for instruction: %y = mul i32 %c, %d
; SPEED:       cost of 1 for instruction: %z = mul i64 %a, 8
; SIZE-LABEL:  'multiply'
; SIZE:        cost of 4 for instruction: %x = mul i64 %a, %b
; LATENCY-LABEL: 'multiply'
; LATENCY:     cost of 14 for instruction: %x = mul i64 %a, %b
  %x = mul i64 %a, %b
  %y = mul i32 %c, %d
  %z = mul i64 %a, 8
  ret void
}

; A division of integers is a call, which is small and slow, unless it is
; by a constant.

define void @divide(i64 %a, i64 %b) {
; SPEED-LABEL: 'divide'
; SPEED:       cost of 20 for instruction: %x = sdiv i64 %a, %b
; SPEED:       cost of 20 for instruction: %y = urem i64 %a, %b
; SPEED:       cost of 1 for instruction: %z = udiv i64 %a, 16
; SPEED:       cost of 4 for instruction: %w = sdiv i64 %a, 16
; SPEED:       cost of 6 for instruction: %v = udiv i64 %a, 10
; SPEED:       cost of 13 for instruction: %u = srem i64 %a, 10
; SIZE-LABEL:  'divide'
; SIZE:        cost of 2 for instruction: %x = sdiv i64 %a, %b
; SIZE:        cost of 2 for instruction: %y = urem i64 %a, %b
; LATENCY-LABEL: 'divide'
; LATENCY:     cost of 40 for instruction: %x = sdiv i64 %a, %b
  %x = sdiv i64 %a, %b
  %y = urem i64 %a, %b
  %z = udiv i64 %a, 16
  %w = sdiv i64 %a, 16
  %v = udiv i64 %a, 10
  %u = srem i64 %a, 10
  ret void
}

; A division of floating-point values is in line, and long.

define void @quotient(double %a, double %b, float %c, float %d) {
; SPEED-LABEL: 'quotient'
; SPEED:       cost of 13 for instruction: %x = fdiv double %a, %b
; SPEED:       cost of 13 for instruction: %y = fdiv float %c, %d
; SIZE-LABEL:  'quotient'
; SIZE:        cost of 13 for instruction: %x = fdiv double %a, %b
; LATENCY-LABEL: 'quotient'
; LATENCY:     cost of 30 for instruction: %x = fdiv double %a, %b
  %x = fdiv double %a, %b
  %y = fdiv float %c, %d
  ret void
}
