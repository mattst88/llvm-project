; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A 32-bit switch value is compared as a word by cmp4 in every block of the
; switch, and is not zero-extended to compare it as a doubleword.
define i64 @sw(i32 %x) {
; CHECK-LABEL: sw:
; CHECK-NOT:   zxt4
; CHECK:       cmp4.lt p{{[0-9]+}}, p{{[0-9]+}} = 2, r32
; CHECK-NOT:   zxt4
; CHECK:       cmp4.eq p{{[0-9]+}}, p{{[0-9]+}} = 0, r32
; CHECK-NOT:   zxt4
; CHECK:       .endp sw
entry:
  switch i32 %x, label %d [
    i32 0, label %a
    i32 1, label %b
    i32 3, label %c
    i32 4, label %e
  ]
a:
  ret i64 10
b:
  ret i64 20
c:
  ret i64 30
e:
  ret i64 40
d:
  ret i64 0
}
