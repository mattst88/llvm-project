; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; Zero is r0. Another constant of 22 bits is added to it, and anything wider
; is a movl.

define i64 @zero() {
; CHECK-LABEL: zero:
; CHECK:       mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 0
}

define i64 @one() {
; CHECK-LABEL: one:
; CHECK:       mov r8 = 1
  ret i64 1
}

define i64 @largest_small() {
; CHECK-LABEL: largest_small:
; CHECK:       mov r8 = 2097151
  ret i64 2097151
}

define i64 @smallest_small() {
; CHECK-LABEL: smallest_small:
; CHECK:       mov r8 = -2097152
  ret i64 -2097152
}

define i64 @smallest_large() {
; CHECK-LABEL: smallest_large:
; CHECK:       movl r8 = 2097152
  ret i64 2097152
}

define i64 @largest_negative_large() {
; CHECK-LABEL: largest_negative_large:
; CHECK:       movl r8 = -2097153
  ret i64 -2097153
}

define i64 @wide() {
; CHECK-LABEL: wide:
; CHECK:       movl r8 = 1311768467463790320
  ret i64 u0x123456789ABCDEF0
}

; The upper bits of a narrow result are whatever the extension asked for
; makes them, and with none asked for they are zero.

define signext i32 @narrow_signed() {
; CHECK-LABEL: narrow_signed:
; CHECK:       mov r8 = -1
  ret i32 -1
}

define i32 @narrow() {
; CHECK-LABEL: narrow:
; CHECK:       movl r8 = 4294967295
  ret i32 -1
}

; An addition takes a constant of 14 bits.

define i64 @add_largest(i64 %a) {
; CHECK-LABEL: add_largest:
; CHECK:       adds r8 = 8191, r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = add i64 %a, 8191
  ret i64 %r
}

define i64 @add_smallest(i64 %a) {
; CHECK-LABEL: add_smallest:
; CHECK:       adds r8 = -8192, r32
  %r = add i64 %a, -8192
  ret i64 %r
}

define i64 @sub_constant(i64 %a) {
; CHECK-LABEL: sub_constant:
; CHECK:       adds r8 = -5, r32
  %r = sub i64 %a, 5
  ret i64 %r
}

define i64 @add_too_large(i64 %a) {
; CHECK-LABEL: add_too_large:
; CHECK:       mov [[T:r[0-9]+]] = 8192 ;;
; CHECK-NEXT:  add r8 = r32, [[T]]
  %r = add i64 %a, 8192
  ret i64 %r
}

; The other operations take one of eight bits.

define i64 @sub_from(i64 %a) {
; CHECK-LABEL: sub_from:
; CHECK:       sub r8 = 127, r32
  %r = sub i64 127, %a
  ret i64 %r
}

define i64 @negate(i64 %a) {
; CHECK-LABEL: negate:
; CHECK:       sub r8 = 0, r32
  %r = sub i64 0, %a
  ret i64 %r
}

define i64 @sub_from_too_large(i64 %a) {
; CHECK-LABEL: sub_from_too_large:
; CHECK:       mov [[T:r[0-9]+]] = 128 ;;
; CHECK-NEXT:  sub r8 = [[T]], r32
  %r = sub i64 128, %a
  ret i64 %r
}

define i64 @and_constant(i64 %a) {
; CHECK-LABEL: and_constant:
; CHECK:       and r8 = -128, r32
  %r = and i64 %a, -128
  ret i64 %r
}

define i64 @or_constant(i64 %a) {
; CHECK-LABEL: or_constant:
; CHECK:       or r8 = 127, r32
  %r = or i64 %a, 127
  ret i64 %r
}

define i64 @xor_constant(i64 %a) {
; CHECK-LABEL: xor_constant:
; CHECK:       xor r8 = 85, r32
  %r = xor i64 %a, 85
  ret i64 %r
}

define i64 @complement(i64 %a) {
; CHECK-LABEL: complement:
; CHECK:       xor r8 = -1, r32
  %r = xor i64 %a, -1
  ret i64 %r
}

define i64 @andcm_constant(i64 %a) {
; CHECK-LABEL: andcm_constant:
; CHECK:       andcm r8 = 15, r32
  %n = xor i64 %a, -1
  %r = and i64 %n, 15
  ret i64 %r
}

define i64 @and_too_large(i64 %a) {
; CHECK-LABEL: and_too_large:
; CHECK:       mov [[T:r[0-9]+]] = 128 ;;
; CHECK-NEXT:  and r8 = r32, [[T]]
  %r = and i64 %a, 128
  ret i64 %r
}

; A 32-bit constant that crosses blocks is extended to 64 bits. Sign
; extended a negative one still fits addl.
define i32 @phi_negative_i32(ptr %p, i32 %x) {
; CHECK-LABEL: phi_negative_i32:
; CHECK-NOT:   movl
; CHECK:       mov r8 = -4
; CHECK-NOT:   movl
; CHECK:       .endp phi_negative_i32
entry:
  %c = icmp sgt i32 %x, 3
  br i1 %c, label %done, label %store

store:
  store i32 %x, ptr %p
  br label %done

done:
  %r = phi i32 [ 0, %store ], [ -4, %entry ]
  ret i32 %r
}

; cmp4 reads only the low 32 bits, so a negative constant too wide for the
; compare's immediate is materialized sign extended.
define i1 @cmp4_negative(i32 %x) {
; CHECK-LABEL: cmp4_negative:
; CHECK:       mov [[T:r[0-9]+]] = -1000999
; CHECK:       cmp4.eq p{{[0-9]+}}, p{{[0-9]+}} = r32, [[T]]
  %c = icmp eq i32 %x, -1000999
  ret i1 %c
}
