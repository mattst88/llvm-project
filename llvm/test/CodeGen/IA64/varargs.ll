; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

; The arguments with no names are read from memory, so those that came in
; registers are written below the ones that did not: the last two in the
; scratch area and the rest at the top of the frame. The caller put nothing
; in a register it had no argument for, and what is there can have its NaT
; bit set, so the registers are spilled, which takes a frame of all eight
; and a copy of ar.unat.
define i64 @first(i32 %n, ...) {
; CHECK-LABEL: first:
; CHECK:       alloc r40 = ar.pfs, 8, 2, 0, 0
; CHECK-NEXT:  .save ar.unat, r41
; CHECK-NEXT:  mov r41 = ar.unat
; CHECK-NEXT:  .fframe 48
; CHECK-NEXT:  adds r12 = -48, r12 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  adds r14 = 8, r12
; CHECK-NEXT:  mov r17 = r12 ;;
; CHECK-NEXT:  adds r15 = 48, r14
; CHECK-NEXT:  adds r16 = 8, r14
; CHECK-NEXT:  st8.spill [r14] = r33 ;;
; CHECK-NEXT:  st8.spill [r15] = r39
; CHECK-NEXT:  adds r15 = 40, r14 ;;
; CHECK-NEXT:  st8.spill [r15] = r38
; CHECK-NEXT:  adds r15 = 32, r14 ;;
; CHECK-NEXT:  st8.spill [r15] = r37
; CHECK-NEXT:  adds r15 = 24, r14 ;;
; CHECK-NEXT:  st8.spill [r15] = r36
; CHECK-NEXT:  st8 [r17] = r16
; CHECK-NEXT:  adds r15 = 16, r14 ;;
; CHECK-NEXT:  st8.spill [r15] = r35
; CHECK-NEXT:  ld8 r15 = [r17] ;;
; CHECK-NEXT:  st8.spill [r16] = r34
; CHECK-NEXT:  ld8 r14 = [r14]
; CHECK-NEXT:  adds r16 = 8, r15 ;;
; CHECK-NEXT:  st8 [r17] = r16
; CHECK-NEXT:  ld8 r15 = [r15] ;;
; CHECK-NEXT:  add r8 = r14, r15
; CHECK-NEXT:  mov ar.unat = r41
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 48, r12
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  %w = va_arg ptr %ap, i64
  call void @llvm.va_end(ptr %ap)
  %r = add i64 %v, %w
  ret i64 %r
}

; A floating-point argument that has a name takes a slot with it, and one
; that has none is its bits in a slot.
define double @fp(i64 %a, double %b, ...) {
; CHECK-LABEL: fp:
; CHECK:       adds r12 = -48, r12 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  st8.spill [r14] = r34
; CHECK-NEXT:  adds r15 = 40, r14
; CHECK-NEXT:  ldfd f6 = [r14] ;;
; CHECK:       or r14 = 8, r14 ;;
; CHECK:       st8.spill [r14] = r35
; CHECK-NEXT:  st8 [r15] = r14
; CHECK-NEXT:  fma.d f8 = f6, f1, f8 ;;
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, double
  %r = fadd double %v, %b
  ret double %r
}

; With every register named there is nothing to write, and the list starts
; after the named arguments in memory.
define i64 @many(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h, i64 %i, ...) {
; CHECK-LABEL: many:
; CHECK-NOT:   r12 = -
; CHECK:       adds r14 = 24, r12
; CHECK:       ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i64
  ret i64 %v
}

; An argument aligned to sixteen bytes starts at such an address.
define i128 @wide(i64 %a, ...) {
; CHECK-LABEL: wide:
; CHECK:       adds r14 = 8, r12
; CHECK:       adds r16 = 15, r14
; CHECK-NEXT:  st8.spill [r14] = r33 ;;
; CHECK:       and r16 = -16, r16 ;;
; CHECK:       or r15 = 8, r16 ;;
; CHECK-NEXT:  .mmf
; CHECK-NEXT:  st8.spill [r14] = r34
; CHECK:       ld8 r8 = [r16] ;;
; CHECK:       ld8 r9 = [r15]
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i128
  ret i128 %v
}

; A copy of a list is a copy of a pointer.
declare i32 @vf(ptr, ptr)
define i32 @forward(ptr %f, ...) {
; CHECK-LABEL: forward:
; CHECK:       alloc r40 = ar.pfs, 8, 3, 2, 0
; CHECK:       adds r14 = 40, r12
; CHECK:       st8.spill [r14] = r33
; CHECK:       .save rp, r41
; CHECK-NEXT:  mov r41 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  st8 [r15] = r14
; CHECK-NEXT:  ld8 r44 = [r15]
; CHECK-NEXT:  br.call.sptk.many b0 = vf ;;
  %ap = alloca ptr
  %aq = alloca ptr
  call void @llvm.va_start(ptr %ap)
  call void @llvm.va_copy(ptr %aq, ptr %ap)
  %l = load ptr, ptr %aq
  %r = call i32 @vf(ptr %f, ptr %l)
  ret i32 %r
}

; The caller puts floating-point arguments that the callee has not named
; in the general registers and in memory as their bits.
declare i32 @pf(ptr, ...)
define i32 @call(double %d, double %e) {
; CHECK-LABEL: call:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 8, 0
; CHECK:       getf.d r37 = f8 ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r14 = 24, r12
; CHECK-NEXT:  adds r15 = 16, r12
; CHECK-NEXT:  mov r36 = 1
; CHECK-NEXT:  mov r39 = 2
; CHECK-NEXT:  getf.d r38 = f9
; CHECK-NEXT:  mov r35 = r0 ;;
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  mov r40 = r37
; CHECK-NEXT:  stfd [r14] = f8
; CHECK-NEXT:  stfd [r15] = f8
; CHECK-NEXT:  mov r41 = r37
; CHECK-NEXT:  mov r42 = r37
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = pf ;;
  %r = call i32 (ptr, ...) @pf(ptr null, i32 1, double %d, double %e, i64 2, double %d, double %d, double %d, double %d, double %d)
  ret i32 %r
}

; One that has a name is still in a floating-point register, and in its slot
; as well: a callee that was declared without its parameters has named all
; its arguments too, as far as a call can tell.
declare i32 @pf2(double, ...)
define i32 @call_named(double %d, float %f) {
; CHECK-LABEL: call_named:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 3, 0
; CHECK:       getf.d r35 = f8
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  getf.s r36 = f9 ;;
; CHECK-NEXT:  mov r37 = r35
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = pf2 ;;
  %r = call i32 (double, ...) @pf2(double %d, float %f, double %d)
  ret i32 %r
}

; A read of a slot comes after the spill that wrote it.
define i64 @third(i64 %a, ...) {
; CHECK-LABEL: third:
; CHECK:       st8.spill [[[A:r[0-9]+]]] = r35
; CHECK:       ld8 r8 = [[[A]]]
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %p = load ptr, ptr %ap
  %q = getelementptr i8, ptr %p, i64 16
  %v = load i64, ptr %q
  ret i64 %v
}
