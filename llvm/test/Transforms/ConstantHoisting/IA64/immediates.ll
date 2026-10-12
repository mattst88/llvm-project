; RUN: opt -passes=consthoist -mtriple=ia64-unknown-linux-gnu -S < %s \
; RUN:   | FileCheck %s

; A constant of more than 22 bits takes two of the three slots of a bundle
; to put in a register, so one that is used twice is put there once.

define i64 @wide(i64 %a, i64 %b) {
; CHECK-LABEL: @wide(
; CHECK:         [[C:%.*]] = bitcast i64 81985529216486895 to i64
; CHECK-NEXT:    %x = add i64 %a, [[C]]
; CHECK-NEXT:    %y = xor i64 %b, [[C]]
  %x = add i64 %a, 81985529216486895
  %y = xor i64 %b, 81985529216486895
  %r = mul i64 %x, %y
  ret i64 %r
}

; One that an add makes from r0 takes one slot, as a copy of it would.

define i64 @one_slot(i64 %a, i64 %b) {
; CHECK-LABEL: @one_slot(
; CHECK-NOT:     bitcast
; CHECK:         %x = xor i64 %a, 1000000
; CHECK-NEXT:    %y = xor i64 %b, 1000000
  %x = xor i64 %a, 1000000
  %y = xor i64 %b, 1000000
  %r = mul i64 %x, %y
  ret i64 %r
}

; A mask that is a run of bits, or all but one, is in the instruction that
; extracts, deposits or clears the field.

define i64 @field(i64 %a, i64 %b) {
; CHECK-LABEL: @field(
; CHECK-NOT:     bitcast
; CHECK:         %x = and i64 %a, 1099511627520
; CHECK-NEXT:    %y = and i64 %b, 1099511627520
; CHECK-NEXT:    %z = or i64 %x, 1152920405095219200
; CHECK-NEXT:    %w = or i64 %y, 1152920405095219200
  %x = and i64 %a, 1099511627520
  %y = and i64 %b, 1099511627520
  %z = or i64 %x, 1152920405095219200
  %w = or i64 %y, 1152920405095219200
  %r = mul i64 %z, %w
  ret i64 %r
}

; A division by a constant is a multiplication by another, which it could
; not be made into with the constant out of sight.

define i64 @divisor(i64 %a, i64 %b) {
; CHECK-LABEL: @divisor(
; CHECK-NOT:     bitcast
; CHECK:         %x = udiv i64 %a, 10000000000
; CHECK-NEXT:    %y = urem i64 %b, 10000000000
  %x = udiv i64 %a, 10000000000
  %y = urem i64 %b, 10000000000
  %r = add i64 %x, %y
  ret i64 %r
}
