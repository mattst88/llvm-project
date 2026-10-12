; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

define i64 @add(i64 %a, i64 %b) {
; CHECK-LABEL: add:
; CHECK:       add r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = add i64 %a, %b
  ret i64 %r
}

define i64 @sub(i64 %a, i64 %b) {
; CHECK-LABEL: sub:
; CHECK:       sub r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = sub i64 %a, %b
  ret i64 %r
}

define i64 @and(i64 %a, i64 %b) {
; CHECK-LABEL: and:
; CHECK:       and r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = and i64 %a, %b
  ret i64 %r
}

define i64 @or(i64 %a, i64 %b) {
; CHECK-LABEL: or:
; CHECK:       or r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = or i64 %a, %b
  ret i64 %r
}

define i64 @xor(i64 %a, i64 %b) {
; CHECK-LABEL: xor:
; CHECK:       xor r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = xor i64 %a, %b
  ret i64 %r
}

define i64 @andcm(i64 %a, i64 %b) {
; CHECK-LABEL: andcm:
; CHECK:       andcm r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %n = xor i64 %b, -1
  %r = and i64 %a, %n
  ret i64 %r
}

define i64 @add_and_one(i64 %a, i64 %b) {
; CHECK-LABEL: add_and_one:
; CHECK:       add r8 = r32, r33, 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %s = add i64 %a, %b
  %r = add i64 %s, 1
  ret i64 %r
}

define i64 @sub_and_one(i64 %a, i64 %b) {
; CHECK-LABEL: sub_and_one:
; CHECK:       sub r8 = r32, r33, 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %s = sub i64 %a, %b
  %r = sub i64 %s, 1
  ret i64 %r
}

; A narrow operation is done on the whole register, and the bits above the
; result are whatever comes of it.
define i32 @add32(i32 %a, i32 %b) {
; CHECK-LABEL: add32:
; CHECK:       add r8 = r32, r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = add i32 %a, %b
  ret i32 %r
}

; One group holds what does not depend on what it computes.
define i64 @groups(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: groups:
; CHECK:       add [[X:r[0-9]+]] = r32, r33
; CHECK-NEXT:  add [[Y:r[0-9]+]] = r34, r35 ;;
; CHECK-NEXT:  xor r8 = [[X]], [[Y]]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %x = add i64 %a, %b
  %y = add i64 %c, %d
  %r = xor i64 %x, %y
  ret i64 %r
}
