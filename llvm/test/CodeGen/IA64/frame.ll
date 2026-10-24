; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The sixteen bytes at the stack pointer belong to whatever is called, so a
; function finds that much room there on entry and needs no frame of its own
; for an object that fits in it.

define i64 @scratch(i64 %a) {
; CHECK-LABEL: scratch:
; CHECK:         adds r14 = 8, r12 ;;
; CHECK-NEXT:    st8 [r14] = r32
; CHECK-NEXT:    ld8 r8 = [r14]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %p = alloca i64
  store volatile i64 %a, ptr %p
  %v = load volatile i64, ptr %p
  ret i64 %v
}

; Twenty-four bytes need sixteen more than that, and the object starts eight
; bytes above the new stack pointer.

define i64 @frame(i64 %a) {
; CHECK-LABEL: frame:
; CHECK:         adds r12 = -16, r12 ;;
; CHECK-NEXT:    .body
; CHECK-NEXT:    adds r14 = 8, r12 ;;
; CHECK-NEXT:    adds r15 = 16, r14
; CHECK-NEXT:    st8 [r14] = r32 ;;
; CHECK-NEXT:    st8 [r15] = r32
; CHECK-NEXT:    ld8 r8 = [r15]
; CHECK-NEXT:    .restore sp
; CHECK-NEXT:    adds r12 = 16, r12
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %p = alloca [3 x i64]
  %q = getelementptr i64, ptr %p, i64 2
  store volatile i64 %a, ptr %p
  store volatile i64 %a, ptr %q
  %v = load volatile i64, ptr %q
  ret i64 %v
}

; An add reaches 8191 bytes. Beyond that the size of the frame goes in r2,
; and the stack pointer still moves in one instruction.

define i64 @large(i64 %a) {
; CHECK-LABEL: large:
; CHECK:         mov r2 = -15984 ;;
; CHECK-NEXT:    .fframe 15984
; CHECK-NEXT:    add r12 = r2, r12
; CHECK-NEXT:    .body
; CHECK-NEXT:    mov r8 = r0 ;;
; CHECK-NEXT:    mov r14 = r12 ;;
; CHECK-NEXT:    st8 [r14] = r32
; CHECK-NEXT:    mov r2 = 15984 ;;
; CHECK-NEXT:    .restore sp
; CHECK-NEXT:    add r12 = r2, r12
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %p = alloca [2000 x i64]
  store volatile i64 %a, ptr %p
  ret i64 0
}

define i64 @huge(i64 %a) {
; CHECK-LABEL: huge:
; CHECK:         movl r2 = -7999984 ;;
; CHECK-NEXT:    .fframe 7999984
; CHECK-NEXT:    add r12 = r2, r12 ;;
; CHECK:         movl r2 = 7999984 ;;
; CHECK-NEXT:    .restore sp
; CHECK-NEXT:    add r12 = r2, r12
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %p = alloca [1000000 x i64]
  store volatile i64 %a, ptr %p
  ret i64 0
}

; The address of an object that an add does not reach from the stack pointer
; is built in the register that is to hold it.

define ptr @far(ptr %q) {
; CHECK-LABEL: far:
; CHECK:         mov r2 = -16384 ;;
; CHECK-NEXT:    .fframe 16384
; CHECK-NEXT:    add r12 = r2, r12
; CHECK-NEXT:    .body
; CHECK-NEXT:    mov r8 = 16384 ;;
; CHECK-NEXT:    .mlx
; CHECK-NEXT:    mov r14 = r12
; CHECK-NEXT:    add r8 = r8, r12 ;;
; CHECK-NEXT:    st8 [r32] = r14
  %a = alloca [16 x i8]
  %b = alloca [16384 x i8]
  store volatile ptr %b, ptr %q
  ret ptr %a
}
