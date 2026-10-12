; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; A run of bits at a position in a register is a field, and one instruction
; extracts a field, deposits one among zeroes, or deposits one among the
; bits of another register. Shifts and masks that come to one of those need
; no constant in a register for the mask.

define i64 @low(i64 %x) {
; CHECK-LABEL: low:
; CHECK:       extr.u r8 = r32, 0, 12
; OBJ-LABEL:   <low>:
; OBJ:         extr.u r8 = r32, 0, 12
  %r = and i64 %x, 4095
  ret i64 %r
}

define i64 @middle(i64 %x) {
; CHECK-LABEL: middle:
; CHECK:       extr.u r8 = r32, 12, 20
; OBJ-LABEL:   <middle>:
; OBJ:         extr.u r8 = r32, 12, 20
  %s = lshr i64 %x, 12
  %r = and i64 %s, 1048575
  ret i64 %r
}

define i64 @masked_first(i64 %x) {
; CHECK-LABEL: masked_first:
; CHECK:       extr.u r8 = r32, 8, 8
  %s = and i64 %x, 65280
  %r = lshr i64 %s, 8
  ret i64 %r
}

define i64 @bit(i64 %x) {
; CHECK-LABEL: bit:
; CHECK:       extr.u r8 = r32, 40, 1
  %s = lshr i64 %x, 40
  %r = and i64 %s, 1
  ret i64 %r
}

define i32 @narrow(i32 %x) {
; CHECK-LABEL: narrow:
; CHECK:       extr.u r8 = r32, 3, 3
  %s = lshr i32 %x, 3
  %r = and i32 %s, 7
  ret i32 %r
}

; Two shifts, left and then at least as far right, leave a field with
; zeroes or with copies of its top bit above it.

define i64 @unsigned(i64 %x) {
; CHECK-LABEL: unsigned:
; CHECK:       extr.u r8 = r32, 32, 12
  %s = shl i64 %x, 20
  %r = lshr i64 %s, 52
  ret i64 %r
}

define i64 @signed(i64 %x) {
; CHECK-LABEL: signed:
; CHECK:       extr r8 = r32, 32, 12
; OBJ-LABEL:   <signed>:
; OBJ:         extr r8 = r32, 32, 12
  %s = shl i64 %x, 20
  %r = ashr i64 %s, 52
  ret i64 %r
}

define i64 @signed_byte(i64 %x) {
; CHECK-LABEL: signed_byte:
; CHECK:       extr r8 = r32, 5, 8
  %s = lshr i64 %x, 5
  %t = trunc i64 %s to i8
  %r = sext i8 %t to i64
  ret i64 %r
}

define i64 @place(i64 %x) {
; CHECK-LABEL: place:
; CHECK:       dep.z r8 = r32, 7, 10
; OBJ-LABEL:   <place>:
; OBJ:         dep.z r8 = r32, 7, 10
  %s = and i64 %x, 1023
  %r = shl i64 %s, 7
  ret i64 %r
}

define i64 @clear(i64 %x) {
; CHECK-LABEL: clear:
; CHECK:       dep r8 = 0, r32, 8, 12
; OBJ-LABEL:   <clear>:
; OBJ:         dep r8 = 0, r32, 8, 12
  %r = and i64 %x, -1048321
  ret i64 %r
}

define i64 @set(i64 %x) {
; CHECK-LABEL: set:
; CHECK:       dep r8 = -1, r32, 12, 8
; OBJ-LABEL:   <set>:
; OBJ:         dep r8 = -1, r32, 12, 8
  %r = or i64 %x, 1044480
  ret i64 %r
}

define i64 @insert(i64 %x, i64 %y) {
; CHECK-LABEL: insert:
; CHECK:       dep r8 = r33, r32, 8, 8
; OBJ-LABEL:   <insert>:
; OBJ:         dep r8 = r33, r32, 8, 8
  %a = and i64 %x, -65281
  %b = shl i64 %y, 8
  %c = and i64 %b, 65280
  %r = or i64 %a, %c
  ret i64 %r
}

define i64 @insert_low(i64 %x, i64 %y) {
; CHECK-LABEL: insert_low:
; CHECK:       dep r8 = r33, r32, 0, 5
  %a = and i64 %x, -32
  %c = and i64 %y, 31
  %r = or i64 %a, %c
  ret i64 %r
}

; The instruction that deposits a register in another takes sixteen bits at
; the most.

define i64 @insert_sixteen(i64 %x, i64 %y) {
; CHECK-LABEL: insert_sixteen:
; CHECK:       dep r8 = r33, r32, 8, 16
  %a = and i64 %x, -16776961
  %b = shl i64 %y, 8
  %c = and i64 %b, 16776960
  %r = or i64 %a, %c
  ret i64 %r
}

define i64 @insert_seventeen(i64 %x, i64 %y) {
; CHECK-LABEL: insert_seventeen:
; CHECK-NOT:   dep r8 = r33
; CHECK:       or r8 =
  %a = and i64 %x, -33553921
  %b = shl i64 %y, 8
  %c = and i64 %b, 33553920
  %r = or i64 %a, %c
  ret i64 %r
}

; A value that may have bits outside the field is not one to deposit.

define i64 @not_a_field(i64 %x, i64 %y) {
; CHECK-LABEL: not_a_field:
; CHECK-NOT:   dep r8 = r33
; CHECK:       or r8 =
  %a = and i64 %x, -65281
  %b = shl i64 %y, 8
  %r = or i64 %a, %b
  ret i64 %r
}

; Eight, sixteen and thirty-two bits have their own instructions, and a mask
; of a few bits is an immediate.

define i64 @byte(i64 %x) {
; CHECK-LABEL: byte:
; CHECK:       zxt1 r8 = r32
  %r = and i64 %x, 255
  ret i64 %r
}

define i64 @few(i64 %x) {
; CHECK-LABEL: few:
; CHECK:       and r8 = 15, r32
  %r = and i64 %x, 15
  ret i64 %r
}
