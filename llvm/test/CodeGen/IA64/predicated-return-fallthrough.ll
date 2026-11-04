; RUN: llc -mcpu=madison < %s | FileCheck %s

; A predicated return ends the block that falls through to the switch's
; default. No branch to the next bundle may follow it. Reduced from CPython's
; advance_with_locations.

; CHECK-LABEL: f:
; CHECK:       (p6) br.ret.dpnt.many b0 ;;
; CHECK:       (p7) br.ret.dptk.many b0 ;;
; CHECK-NEXT:  .LBB0_2:

target datalayout = "e-m:e-p:64:64-i64:64-i128:128-f80:128-n32:64-S128"
target triple = "ia64-unknown-linux-gnu"

define fastcc void @f(ptr %bounds, ptr %endline, i32 %and) {
entry:
  switch i32 %and, label %sw.bb [
    i32 13, label %sw.bb19
    i32 14, label %sw.bb7
  ]

sw.bb:
  store i32 1, ptr %endline, align 4
  %ar_line = getelementptr i8, ptr %bounds, i64 8
  store i32 0, ptr %ar_line, align 8
  store i32 0, ptr null, align 4
  ret void

sw.bb7:
  ret void

sw.bb19:
  ret void
}
