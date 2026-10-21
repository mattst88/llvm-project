; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The stack pointer is aligned to sixteen bytes, so a frame with an object
; that wants more aligns the stack pointer itself, and keeps the one it was
; entered with to go back to. The objects of the frame are then found from
; the stack pointer, and the arguments on the stack from where it was.

declare void @use(ptr)

define i64 @aligned(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g,
                    i64 %h, i64 %i) {
; CHECK-LABEL: aligned:
; CHECK:       // %bb.0:
; CHECK-NEXT:    .save ar.pfs, r34
; CHECK-NEXT:    alloc r34 = ar.pfs, 0, 5, 1, 0
; CHECK-NEXT:    .vframe r36
; CHECK-NEXT:    mov r36 = r12
; CHECK-NEXT:    adds r12 = -112, r12 ;;
; CHECK-NEXT:    and r12 = -64, r12
; CHECK-NEXT:    adds r14 = 16, r36
; CHECK-NEXT:    mov r33 = r1 ;;
; CHECK-NEXT:    adds r37 = 64, r12
; CHECK-NEXT:    ld8 r32 = [r14]
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = use ;;
; CHECK-NEXT:    mov r8 = r32
; CHECK-NEXT:    mov ar.pfs = r34
; CHECK-NEXT:    mov b0 = r35
; CHECK-NEXT:    mov r1 = r33
; CHECK-NEXT:    .restore sp
; CHECK-NEXT:    mov r12 = r36
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %x = alloca [64 x i8], align 64
  call void @use(ptr %x)
  ret i64 %i
}

; An alignment the and has no immediate for.

define void @page() {
; CHECK-LABEL: page:
; CHECK:         mov r35 = r12
; CHECK-NEXT:    adds r12 = -8176, r12
; CHECK-NEXT:    mov r2 = -4096 ;;
; CHECK-NEXT:    and r12 = r2, r12
; CHECK:         mov r12 = r35
  %x = alloca [4096 x i8], align 4096
  call void @use(ptr %x)
  ret void
}

; When the stack pointer moves in the body as well, the objects are found
; from a copy of what the prologue left.

define void @variable(i64 %n) {
; CHECK-LABEL: variable:
; CHECK:         mov r35 = r12
; CHECK-NEXT:    adds r12 = -112, r12 ;;
; CHECK-NEXT:    and r12 = -64, r12 ;;
; CHECK:         mov r36 = r12
; CHECK:         sub r12 = r12, r14 ;;
; CHECK-NEXT:    adds r37 = 16, r12
; CHECK-NEXT:    .save rp, r34
; CHECK-NEXT:    mov r34 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = use ;;
; CHECK-NEXT:    adds r37 = 64, r36
; CHECK-NEXT:    mov r1 = r32
; CHECK-NEXT:    br.call.sptk.many b0 = use ;;
; CHECK:         mov r12 = r35
  %x = alloca [64 x i8], align 64
  %y = alloca i8, i64 %n
  call void @use(ptr %y)
  call void @use(ptr %x)
  ret void
}

; A function that does not trust the stack pointer it is entered with aligns
; it to the sixteen bytes that what it calls expects, whatever its own
; objects want.

define void @forced() "stackrealign" {
; CHECK-LABEL: forced:
; CHECK:         .vframe r35
; CHECK-NEXT:    mov r35 = r12
; CHECK-NEXT:    adds r12 = -16, r12 ;;
; CHECK-NEXT:    .mmi
; CHECK-NEXT:    and r12 = -16, r12
; CHECK:         br.call.sptk.many b0 = use ;;
; CHECK:         .restore sp
; CHECK-NEXT:    mov r12 = r35
  %x = alloca i8
  call void @use(ptr %x)
  ret void
}
