; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; A call to a function that was declared without its parameters is a call
; with a variable number of arguments, all of which the callee may have named.
; If it has, it looks for a floating-point one in a register of its own. If
; it turns out to take a variable number of them, it looks for the bits in
; the slot. So the argument is in both.

declare i32 @kr(...)
declare i32 @printf(ptr, ...)

define void @both(i32 %a, double %d, float %f) {
; CHECK-LABEL: both:
; CHECK:         getf.d r37 = f8
; CHECK-NEXT:    mov r33 = r1
; CHECK-NEXT:    mov r36 = r32
; CHECK-NEXT:    mov r39 = 7
; CHECK-NEXT:    getf.s r38 = f9
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0 ;;
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = kr ;;
  call i32 (i32, double, float, i32, ...) @kr(i32 %a, double %d, float %f, i32 7)
  ret void
}

; An extended one is two slots, the first of them an even one, and its bits
; go through memory to get there.

define void @both_extended(i32 %a, x86_fp80 %l) {
; CHECK-LABEL: both_extended:
; CHECK:         mov r36 = r32
; CHECK:         stfe [r14] = f8
; CHECK-NEXT:    or r15 = 8, r14 ;;
; CHECK-NEXT:    ld8 r39 = [r15]
; CHECK-NEXT:    ld8 r38 = [r14]
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = kr ;;
  call i32 (i32, x86_fp80, ...) @kr(i32 %a, x86_fp80 %l)
  ret void
}

; Past the eighth slot it is in memory, and in no register.

define void @both_memory(double %d) {
; CHECK-LABEL: both_memory:
; CHECK:         adds r14 = 16, r12
; CHECK-NEXT:    .mmi
; CHECK-NEXT:    getf.d r42 = f8
; CHECK-NOT:     f9
; CHECK:         stfd [r14] = f8
; CHECK-NEXT:    .save rp, r34
; CHECK-NEXT:    mov r34 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = kr ;;
  call i32 (i64, i64, i64, i64, i64, i64, i64, double, double, ...) @kr(i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, double %d, double %d)
  ret void
}

; One that the callee is known not to have named is in the slot alone.

define void @unnamed(ptr %s, double %d) {
; CHECK-LABEL: unnamed:
; CHECK:         getf.d r37 = f8
; CHECK-NEXT:    mov r33 = r1
; CHECK-NEXT:    mov r36 = r32
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0 ;;
; CHECK-NEXT:    .body
; CHECK-NEXT:    br.call.sptk.many b0 = printf ;;
  call i32 (ptr, ...) @printf(ptr %s, double %d)
  ret void
}

; A function that takes a variable number of arguments reads the ones it has
; named from where a function that takes no others would, and the slots it
; saves for the rest begin after theirs.

define double @named(double %a, float %b, x86_fp80 %c, ...) {
; CHECK-LABEL: named:
; CHECK:         fma.d f6 = f9, f1, f0
; CHECK-NEXT:    fma.d f7 = f10, f1, f0 ;;
; CHECK:         st8.spill [r14] = r36
; CHECK:         st8.spill [r15] = r39
; CHECK-NOT:     r35
; CHECK:         fma.d f6 = f8, f1, f6 ;;
; CHECK:         fma.d f8 = f6, f1, f7 ;;
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %x = fpext float %b to double
  %y = fptrunc x86_fp80 %c to double
  %r = fadd double %a, %x
  %q = fadd double %r, %y
  ret double %q
}

declare void @llvm.va_start(ptr)
