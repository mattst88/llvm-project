; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A function goes from its arguments to its result by way of the calls it
; makes, and none of it by way of SelectionDAG.

declare i32 @g(i32, double, ptr)
declare signext i16 @h()

; The global pointer is kept in a register the calls leave alone, and each
; of them is followed by its return to r1.
define signext i32 @narrow(i32 signext %a, double %b, ptr %p, i8 zeroext %c) {
; CHECK-LABEL: narrow:
; CHECK:       alloc r36 = ar.pfs, 4, 2, 3, 0
; CHECK:       mov r38 = r32
; CHECK-NEXT:  mov r40 = r34
; CHECK-NEXT:  mov r33 = r1 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r32 = r8
; CHECK-NEXT:  br.call.sptk.many b0 = h ;;
; CHECK-NEXT:  zxt1 r14 = r35
; CHECK-NEXT:  sxt2 r15 = r8
; CHECK-NEXT:  mov r1 = r33 ;;
; CHECK-NEXT:  add r14 = r32, r14 ;;
; CHECK-NEXT:  add r14 = r14, r15 ;;
; CHECK-NEXT:  sxt4 r8 = r14
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call i32 @g(i32 %a, double %b, ptr %p)
  %s = call signext i16 @h()
  %t = sext i16 %s to i32
  %u = add i32 %r, %t
  %z = zext i8 %c to i32
  %v = add i32 %u, %z
  ret i32 %v
}

; The entry point is the first word of what the pointer is the address of,
; and the callee's global pointer the second.
define i64 @indirect(ptr %fp, i64 %a) {
; CHECK-LABEL: indirect:
; CHECK:       ld8 r14 = [r32]
; CHECK-NEXT:  adds r15 = 8, r32
; CHECK-NEXT:  mov r36 = r33
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  ld8 r15 = [r15]
; CHECK-NEXT:  mov b6 = r14 ;;
; CHECK-NEXT:  mov r1 = r15
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
; CHECK-NEXT:  mov r1 = r32
  %r = call i64 %fp(i64 %a)
  ret i64 %r
}
