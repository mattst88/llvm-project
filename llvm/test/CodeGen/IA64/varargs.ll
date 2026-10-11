; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
declare void @llvm.va_copy(ptr, ptr)

; The arguments with no names are read from memory, so those that came in
; registers are written below the ones that did not: the last two in the
; scratch area and the rest at the top of the frame.
define i64 @first(i32 %n, ...) {
; CHECK-LABEL: first:
; CHECK:       adds r12 = -48, r12 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  adds r14 = 56, r12 ;;
; CHECK-NEXT:  st8 [r14] = r39
; CHECK-NEXT:  adds r14 = 48, r12 ;;
; CHECK-NEXT:  st8 [r14] = r38
; CHECK-NEXT:  adds r14 = 40, r12 ;;
; CHECK-NEXT:  st8 [r14] = r37
; CHECK-NEXT:  adds r14 = 32, r12 ;;
; CHECK-NEXT:  st8 [r14] = r36
; CHECK-NEXT:  adds r14 = 24, r12 ;;
; CHECK-NEXT:  st8 [r14] = r35
; CHECK-NEXT:  adds r14 = 8, r12 ;;
; CHECK-NEXT:  adds r14 = 16, r14
; CHECK-NEXT:  mov r15 = r12 ;;
; CHECK-NEXT:  st8 [r15] = r14
; CHECK-NEXT:  adds r14 = 8, r12 ;;
; CHECK-NEXT:  st8 [r14] = r33
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  st8 [r14] = r34
; CHECK-NEXT:  add r8 = r33, r34
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
; CHECK:       adds r14 = 16, r12 ;;
; CHECK-NEXT:  or r14 = 8, r14
; CHECK-NEXT:  adds r15 = 8, r12 ;;
; CHECK-NEXT:  st8 [r15] = r14
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  st8 [r14] = r34
; CHECK-NEXT:  setf.d f6 = r34 ;;
; CHECK-NEXT:  fma.d f8 = f6, f1, f8
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
; CHECK:       adds r14 = 24, r12 ;;
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
; CHECK:       adds r14 = 8, r12 ;;
; CHECK-NEXT:  st8 [r14] = r33
; CHECK-NEXT:  adds r14 = 8, r12 ;;
; CHECK-NEXT:  adds r14 = 15, r14 ;;
; CHECK-NEXT:  and r14 = -16, r14 ;;
; CHECK:       ld8 r8 = [r14]
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i128
  ret i128 %v
}

; A copy of a list is a copy of a pointer.
declare i32 @vf(ptr, ptr)
define i32 @forward(ptr %f, ...) {
; CHECK-LABEL: forward:
; CHECK:       alloc r41 = ar.pfs, 8, 3, 2, 0
; CHECK:       adds r14 = 40, r12 ;;
; CHECK-NEXT:  st8 [r14] = r33
; CHECK-NEXT:  adds r44 = 40, r12
; CHECK:       br.call.sptk.many b0 = vf ;;
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
; CHECK:       adds r14 = 24, r12 ;;
; CHECK-NEXT:  stfd [r14] = f8
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  stfd [r14] = f8
; CHECK-NEXT:  getf.d r38 = f9
; CHECK-NEXT:  getf.d r37 = f8
; CHECK-NEXT:  mov r36 = 1
; CHECK-NEXT:  mov r39 = 2
; CHECK-NEXT:  mov r35 = r0 ;;
; CHECK-NEXT:  mov r40 = r37
; CHECK-NEXT:  mov r41 = r37
; CHECK-NEXT:  mov r42 = r37
; CHECK-NEXT:  br.call.sptk.many b0 = pf ;;
  %r = call i32 (ptr, ...) @pf(ptr null, i32 1, double %d, double %e, i64 2, double %d, double %d, double %d, double %d, double %d)
  ret i32 %r
}

; One that has a name is still in a floating-point register.
declare i32 @pf2(double, ...)
define i32 @call_named(double %d, float %f) {
; CHECK-LABEL: call_named:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 3, 0
; CHECK:       getf.s r36 = f9
; CHECK-NEXT:  getf.d r37 = f8 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = pf2 ;;
  %r = call i32 (double, ...) @pf2(double %d, float %f, double %d)
  ret i32 %r
}
