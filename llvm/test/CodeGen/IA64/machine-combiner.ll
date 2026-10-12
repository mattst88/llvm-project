; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A chain of additions, or of the logical operations, is as long as it has
; operations. Two of three can be made at once, in one instruction group,
; and the result is there a group sooner.

define i64 @sum(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: sum:
; CHECK:       add r14 = r32, r33
; CHECK-NEXT:  add r15 = r34, r35 ;;
; CHECK-NEXT:  add r8 = r14, r15
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = add i64 %a, %b
  %y = add i64 %x, %c
  %z = add i64 %y, %d
  ret i64 %z
}

define i64 @exclusive(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: exclusive:
; CHECK:       xor r14 = r32, r33
; CHECK-NEXT:  xor r15 = r34, r35 ;;
; CHECK-NEXT:  xor r8 = r14, r15
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = xor i64 %a, %b
  %y = xor i64 %x, %c
  %z = xor i64 %y, %d
  ret i64 %z
}

define i64 @inclusive(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: inclusive:
; CHECK:       or r14 = r32, r33
; CHECK-NEXT:  or r15 = r34, r35 ;;
; CHECK-NEXT:  or r8 = r14, r15
  %x = or i64 %a, %b
  %y = or i64 %x, %c
  %z = or i64 %y, %d
  ret i64 %z
}

define i64 @conjunction(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: conjunction:
; CHECK:       and r14 = r32, r33
; CHECK-NEXT:  and r15 = r34, r35 ;;
; CHECK-NEXT:  and r8 = r14, r15
  %x = and i64 %a, %b
  %y = and i64 %x, %c
  %z = and i64 %y, %d
  ret i64 %z
}

; What comes late is added last: the two values that are there are summed
; while the product is made.
define i64 @late(ptr %p, i64 %b, i64 %c) {
; CHECK-LABEL: late:
; CHECK:       ld8 r14 = [r32]
; CHECK-NEXT:  add r15 = r33, r34 ;;
; CHECK:       getf.sig r14 = f6 ;;
; CHECK-NEXT:  add r8 = r14, r15
  %a = load i64, ptr %p
  %m = mul i64 %a, %a
  %x = add i64 %m, %b
  %y = add i64 %x, %c
  ret i64 %y
}

; What is subtracted one after another is added up and subtracted once.
define i64 @difference(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: difference:
; CHECK:       sub r14 = r32, r33
; CHECK-NEXT:  add r15 = r34, r35 ;;
; CHECK-NEXT:  sub r8 = r14, r15
  %x = sub i64 %a, %b
  %y = sub i64 %x, %c
  %z = sub i64 %y, %d
  ret i64 %z
}

define i64 @mixed(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: mixed:
; CHECK:       add r14 = r32, r33
; CHECK-NEXT:  sub r15 = r34, r35 ;;
; CHECK-NEXT:  sub r8 = r14, r15
  %x = add i64 %a, %b
  %y = sub i64 %x, %c
  %z = add i64 %y, %d
  ret i64 %z
}
