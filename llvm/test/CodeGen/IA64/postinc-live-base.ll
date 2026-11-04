; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; An access that steps its address register is split from the step where
; the old address is used afterwards, so that no copy of the address goes
; ahead of the access in a group of its own.

; CHECK-LABEL: live:
; CHECK-NOT:     mov
; CHECK:         adds [[Q:r[0-9]+]] = 1, r32
; CHECK-NEXT:    ld1 {{r[0-9]+}} = [r32] ;;
; CHECK-NEXT:    ld1 {{r[0-9]+}} = [[[Q]]] ;;
define i64 @live(ptr %p) {
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %a2 = zext i8 %a to i64
  %b2 = zext i8 %b to i64
  %s = shl i64 %b2, 8
  %r = or i64 %s, %a2
  %t = ptrtoint ptr %p to i64
  %u = add i64 %r, %t
  ret i64 %u
}

; The same where the old address is used in another block.

; CHECK-LABEL: other_block:
; CHECK-NOT:     mov
; CHECK:         ld1 {{r[0-9]+}} = [r32]
; CHECK-NOT:     ld1 {{r[0-9]+}} = [{{r[0-9]+}}], 1
; CHECK:         br.ret
define i64 @other_block(ptr %p, i1 %c) {
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %a2 = zext i8 %a to i64
  %b2 = zext i8 %b to i64
  %r = add i64 %a2, %b2
  br i1 %c, label %t, label %f
t:
  %pi = ptrtoint ptr %p to i64
  %u = add i64 %r, %pi
  ret i64 %u
f:
  ret i64 %r
}

; Where nothing else uses it, the access keeps the step.

; CHECK-LABEL: dead:
; CHECK:         ld1 {{r[0-9]+}} = [r32], 1 ;;
; CHECK-NEXT:    ld1 {{r[0-9]+}} = [r32] ;;
define i64 @dead(ptr %p) {
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %a2 = zext i8 %a to i64
  %b2 = zext i8 %b to i64
  %s = shl i64 %b2, 8
  %r = or i64 %s, %a2
  ret i64 %r
}
