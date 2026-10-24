; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; ((x ^ y) & 0xff) == 0 for an x with no bits above the mask, as CoreMark's
; list search compares a byte it loads: the mask moves to y and the compare
; takes x and y, with no xor or zxt1 of x ahead of it.

define i1 @masked(ptr %p, i16 %y) {
; CHECK-LABEL: masked:
; CHECK:         ld1.s r14 = [r32]
; CHECK-NEXT:    zxt1 r15 = r33
; CHECK-NEXT:    mov r8 = r0 ;;
; CHECK-NEXT:    cmp.eq p6, p7 = r14, r15 ;;
  %v = call i64 @llvm.ia64.ld1.s(ptr %p)
  %t = trunc i64 %v to i16
  %x = xor i16 %t, %y
  %m = and i16 %x, 255
  %c = icmp eq i16 %m, 0
  ret i1 %c
}

; The same with x from another block, known zero-extended there.
define i1 @other_block(ptr %p, i16 %y, i1 %b) {
; CHECK-LABEL: other_block:
; CHECK:         ld1.s r14 = [r32]
; CHECK:         zxt1 r15 = r33 ;;
; CHECK-NEXT:    cmp.eq p7, p6 = r14, r15 ;;
  %v = call i64 @llvm.ia64.ld1.s(ptr %p)
  %t = trunc i64 %v to i16
  br i1 %b, label %a, label %z
a:
  %x = xor i16 %t, %y
  %m = and i16 %x, 255
  %c = icmp ne i16 %m, 0
  ret i1 %c
z:
  ret i1 0
}

; x may have bits above the mask: the xor and mask stay.
define i1 @wide(ptr %p, i16 %y) {
; CHECK-LABEL: wide:
; CHECK:         ld2.s r14 = [r32]
; CHECK-NEXT:    mov r8 = r0 ;;
; CHECK-NEXT:    xor r14 = r14, r33 ;;
; CHECK-NEXT:    zxt1 r14 = r14 ;;
; CHECK-NEXT:    cmp.eq p6, p7 = 0, r14 ;;
  %v = call i64 @llvm.ia64.ld2.s(ptr %p)
  %t = trunc i64 %v to i16
  %x = xor i16 %t, %y
  %m = and i16 %x, 255
  %c = icmp eq i16 %m, 0
  ret i1 %c
}

; (x ^ y) == 0 is x == y.
define i1 @xor_zero(i64 %x, i64 %y) {
; CHECK-LABEL: xor_zero:
; CHECK:         cmp.eq p7, p6 = r32, r33
; CHECK-NOT:     xor {{r[0-9]+}}
  %d = xor i64 %x, %y
  %c = icmp ne i64 %d, 0
  ret i1 %c
}
