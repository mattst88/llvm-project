; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The ninth argument and those after it are in memory, the first of them
; just above the sixteen bytes of scratch area at the stack pointer.

define i64 @incoming(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g,
                     i64 %h, i64 %i, i64 %j) {
; CHECK-LABEL: incoming:
; CHECK:       adds r14 = 24, r12
; CHECK-NEXT:  adds r15 = 16, r12 ;;
; CHECK-NEXT:  ld8 r14 = [r14]
; CHECK-NEXT:  ld8 r15 = [r15] ;;
; CHECK-NEXT:  add r8 = r15, r14
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = add i64 %i, %j
  ret i64 %r
}

; The frame is the scratch area for the callee; the two arguments in memory
; fit in the one this function was given.
define i64 @outgoing(i64 %a) {
; CHECK-LABEL: outgoing:
; CHECK:       alloc r34 = ar.pfs, 1, 3, 8, 0
; CHECK-NEXT:  .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12 ;;
; CHECK-NEXT:  .mmf
; CHECK-NEXT:  adds r14 = 24, r12
; CHECK-NEXT:  mov r15 = 10
; CHECK-NEXT:  adds r16 = 16, r12
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  mov r36 = 1
; CHECK:       mov r43 = 8 ;;
; CHECK-NEXT:  st8 [r14] = r15
; CHECK-NEXT:  st8 [r16] = r32
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = incoming ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
  %r = call i64 @incoming(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
                          i64 8, i64 %a, i64 10)
  ret i64 %r
}

declare void @sink(ptr)

; Incoming arguments, an object and outgoing arguments, from the top of the
; frame down.
define i64 @both(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g,
                 i64 %h, i64 %i, i64 %j) {
; CHECK-LABEL: both:
; CHECK:       adds r12 = -48, r12
; CHECK:       adds r14 = 64, r12
; CHECK:       adds r15 = 72, r12
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  adds r37 = 40, r12 ;;
; CHECK-NEXT:  ld8 r34 = [r14]
; CHECK-NEXT:  ld8 r32 = [r15]
; CHECK-NEXT:  .save rp, r36
; CHECK-NEXT:  mov r36 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = sink ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r14 = 24, r12
; CHECK-NEXT:  adds r15 = 16, r12
; CHECK:       st8 [r14] = r32
; CHECK-NEXT:  st8 [r15] = r34
; CHECK-NEXT:  br.call.sptk.many b0 = incoming ;;
; CHECK:       adds r12 = 48, r12
  %p = alloca [3 x i64]
  call void @sink(ptr %p)
  %r = call i64 @incoming(i64 %j, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
                          i64 8, i64 %i, i64 %j)
  ret i64 %r
}
