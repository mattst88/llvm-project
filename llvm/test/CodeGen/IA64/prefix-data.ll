; RUN: llc -mtriple=ia64 < %s | FileCheck %s
; RUN: llc -mtriple=ia64 -filetype=obj < %s | llvm-nm -n - \
; RUN:   | FileCheck %s --check-prefix=NM

; Prefix data comes before the procedure, whose first bundle is at the next
; multiple of 16.

define void @f() prefix i32 7 {
; CHECK:      .p2align 6
; CHECK:      data4.ua 7
; CHECK-NEXT: .proc f
; CHECK-NEXT: f:
  ret void
}

define void @g() prefix i32 9 {
  ret void
}

; NM: 0000000000000010 T f
; NM: 0000000000000050 T g
