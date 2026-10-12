; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An argument aligned to sixteen bytes starts at an even slot, and the odd
; one before it goes unused.
define i128 @second(i64 %a, i128 %b) {
; CHECK-LABEL: second:
; CHECK:       mov r9 = r35
; CHECK-NEXT:  mov r8 = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i128 %b
}

define i64 @after(i64 %a, i128 %b, i64 %c) {
; CHECK-LABEL: after:
; CHECK:       mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 %c
}

; The eighth slot is the last register, and is not half of an argument.
define i128 @eighth(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i128 %h) {
; CHECK-LABEL: eighth:
; CHECK:       adds r14 = 16, r12 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  adds r14 = 24, r12 ;;
; CHECK-NEXT:  ld8 r9 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i128 %h
}

; The slots are numbered on into memory.
define i128 @tenth(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h, i64 %i, i128 %j) {
; CHECK-LABEL: tenth:
; CHECK:       adds r14 = 32, r12 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  adds r14 = 40, r12 ;;
; CHECK-NEXT:  ld8 r9 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i128 %j
}

declare i64 @g(i64, i128, i64)
define i64 @call() {
; CHECK-LABEL: call:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 5, 0
; CHECK:       mov r35 = 1
; CHECK-NEXT:  mov r37 = 2
; CHECK-NEXT:  mov r39 = 3
; CHECK-NEXT:  mov r38 = r0
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
  %r = call i64 @g(i64 1, i128 2, i64 3)
  ret i64 %r
}

; One with no name is found by aligning the address of the next slot.
declare i64 @h(i64, ...)
define i64 @call_variable(i128 %x) {
; CHECK-LABEL: call_variable:
; CHECK:       adds r12 = -32, r12
; CHECK:       adds r14 = 40, r12
; CHECK-NEXT:  adds r15 = 16, r12
; CHECK-NEXT:  mov r16 = 9
; CHECK:       st8 [r14] = r33
; CHECK-NEXT:  adds r14 = 32, r12
; CHECK:       st8 [r14] = r32
; CHECK-NEXT:  st8 [r15] = r16
  %r = call i64 (i64, ...) @h(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 9, i128 %x)
  ret i64 %r
}
