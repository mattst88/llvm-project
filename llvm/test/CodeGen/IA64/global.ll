; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; CHECK:      .globl word
; CHECK:      .p2align 2
; CHECK-LABEL: word:
; CHECK-NEXT:   data4.ua 7
; CHECK-NEXT:   .size word, 4
@word = global i32 7

; CHECK:      .globl ptr
; CHECK:      .p2align 3
; CHECK-LABEL: ptr:
; CHECK-NEXT:   data8.ua 0
; CHECK-NEXT:   .size ptr, 8
@ptr = global ptr null

; CHECK:      .globl quad
; CHECK-LABEL: quad:
; CHECK-NEXT:   data8.ua 1311768467294899696
@quad = global i64 u0x1234567890ABCDF0

; CHECK:      .globl byte
; CHECK-LABEL: byte:
; CHECK-NEXT:   data1 1
@byte = global i8 1
