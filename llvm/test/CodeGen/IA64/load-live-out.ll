; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A load zero-extends what it reads, so a loaded value used in another block
; needs no further zero extension there.

declare void @g()

define i64 @phi(ptr %p, ptr %q, i1 %c) {
; CHECK-LABEL: phi:
; CHECK:       (p6) ld4 r8 = [r33]{{$}}
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

; Signed compares in other blocks would rather have the word sign-extended,
; but that would put an sxt4 after the load for every user, including a mask
; of its low bits. The signed compares are of its low word, which needs no
; extension.
define i32 @signed(ptr %p, i1 %c) {
; CHECK-LABEL: signed:
; CHECK:       ld4 [[V:r[0-9]+]] = [r32]
; CHECK-NOT:   sxt4
; CHECK:       br.call.sptk.many b0 = g
; CHECK-NOT:   sxt4
; CHECK:       cmp4.lt p7, p6 = -1, [[V]]
; CHECK-NOT:   sxt4
; CHECK:       and {{r[0-9]+}} = 63, [[V]]
; CHECK-NOT:   sxt4
; CHECK:       .endp signed
entry:
  %v = load i32, ptr %p
  br i1 %c, label %l, label %d
l:
  call void @g()
  br label %d
d:
  %op = and i32 %v, 63
  %neg = icmp slt i32 %v, 0
  br i1 %neg, label %a, label %b
a:
  %t = icmp sgt i32 %v, -1
  %tt = zext i1 %t to i32
  ret i32 %tt
b:
  %x = icmp sgt i32 %v, 7
  %xx = zext i1 %x to i32
  %r = add i32 %op, %xx
  ret i32 %r
}
