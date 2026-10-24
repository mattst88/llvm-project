; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The first eight arguments are in the input registers, and a result is in r8.

; A function starts at a line of the instruction cache, and is a procedure to
; the assembler.
; CHECK:       .p2align 6
; CHECK:       .proc nothing
define void @nothing() {
; CHECK-LABEL: nothing:
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .endp nothing
  ret void
}

define i64 @first(i64 %a) {
; CHECK-LABEL: first:
; CHECK:       mov r8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0
  ret i64 %a
}

define i64 @eighth(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g,
                   i64 %h) {
; CHECK-LABEL: eighth:
; CHECK:       mov r8 = r39
; CHECK-NEXT:  br.ret.sptk.many b0
  ret i64 %h
}

; A narrow value has a whole slot, and what is above it is not defined.
define i32 @narrow(i8 %a, i32 %b) {
; CHECK-LABEL: narrow:
; CHECK:       mov r8 = r33
; CHECK-NEXT:  br.ret.sptk.many b0
  ret i32 %b
}

define ptr @pointer(ptr %p) {
; CHECK-LABEL: pointer:
; CHECK:       mov r8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0
  ret ptr %p
}

; A result of more than one word goes on into r9.
define { i64, i64 } @pair(i64 %a, i64 %b) {
; CHECK-LABEL: pair:
; CHECK:       mov r8 = r33
; CHECK-NEXT:  mov r9 = r32
; CHECK-NEXT:  br.ret.sptk.many b0
  %x = insertvalue { i64, i64 } poison, i64 %b, 0
  %y = insertvalue { i64, i64 } %x, i64 %a, 1
  ret { i64, i64 } %y
}
