; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A load zero-extends what it reads, so a loaded value used in another block
; needs no further zero extension there.

declare void @g()

define i64 @phi(ptr %p, ptr %q, i1 %c) {
; CHECK-LABEL: phi:
; CHECK:       (p6) ld4 r8 = [r33] ;;
; CHECK-NEXT:  (p7) ld4 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  %a = load i32, ptr %p
  br i1 %c, label %l, label %d
l:
  %b = load i32, ptr %q
  br label %d
d:
  %v = phi i32 [ %a, %entry ], [ %b, %l ]
  %z = zext i32 %v to i64
  ret i64 %z
}

; The top byte of the word is all that a shift leaves.
define i64 @top_byte(ptr %p, i1 %c) {
; CHECK-LABEL: top_byte:
; CHECK:       ld4 r32 = [r32]
; CHECK:       br.call.sptk.many b0 = g
; CHECK:       shr.u r8 = r32, 24
entry:
  %a = load i32, ptr %p
  br i1 %c, label %l, label %d
l:
  call void @g()
  br label %d
d:
  %s = lshr i32 %a, 24
  %z = zext i32 %s to i64
  ret i64 %z
}
