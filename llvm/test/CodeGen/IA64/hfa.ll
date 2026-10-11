; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; An aggregate whose members are all of one floating-point type is an array
; of that type here. It has the slots its bytes would fill, two
; single-precision members to a slot, and each member that is in one of the
; first eight slots is in the next of f8 to f15.

define float @third(i32 %i, [3 x float] %h) {
; CHECK-LABEL: third:
; CHECK:       mov f8 = f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = extractvalue [3 x float] %h, 2
  ret float %c
}

; Three of them are two slots.
define i64 @after_three(i32 %i, [3 x float] %h, i64 %n) {
; CHECK-LABEL: after_three:
; CHECK:       mov r8 = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 %n
}

define double @doubles(i32 %i, [2 x double] %h, double %d) {
; CHECK-LABEL: doubles:
; CHECK:       fma.d f8 = f9, f1, f10
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %y = extractvalue [2 x double] %h, 1
  %r = fadd double %y, %d
  ret double %r
}

; A member whose slot is in memory is in memory, whatever registers are left.
define float @in_memory(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, [3 x float] %h) {
; CHECK-LABEL: in_memory:
; CHECK:       fma.s f6 = f8, f1, f9
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  ldfs f7 = [r14] ;;
; CHECK-NEXT:  fma.s f8 = f6, f1, f7
  %x = extractvalue [3 x float] %h, 0
  %y = extractvalue [3 x float] %h, 1
  %z = extractvalue [3 x float] %h, 2
  %r = fadd float %x, %y
  %s = fadd float %r, %z
  ret float %s
}

; When the registers run out before the slots do, the rest are their bits in
; the slots: two halves of one general register.
define float @no_register(double %a, double %b, double %c, double %d, double %e, double %f, [4 x float] %h) {
; CHECK-LABEL: no_register:
; CHECK:       setf.s f6 = r39
; CHECK-NEXT:  shr.u r14 = r39, 32 ;;
; CHECK-NEXT:  setf.s f7 = r14 ;;
; CHECK-NEXT:  fms.s f8 = f6, f1, f7
  %x = extractvalue [4 x float] %h, 2
  %y = extractvalue [4 x float] %h, 3
  %r = fsub float %x, %y
  ret float %r
}

; The second half of a slot whose first half had the last register is in the
; low half of the general register, where GCC puts it and looks for it.
define float @odd_half(double %a, double %b, double %c, double %d, double %e, double %f, double %g, [3 x float] %h) {
; CHECK-LABEL: odd_half:
; CHECK:       setf.s f6 = r39 ;;
; CHECK-NEXT:  fma.s f6 = f15, f1, f6
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  ldfs f7 = [r14] ;;
  %x = extractvalue [3 x float] %h, 0
  %y = extractvalue [3 x float] %h, 1
  %z = extractvalue [3 x float] %h, 2
  %r = fadd float %x, %y
  %s = fadd float %r, %z
  ret float %s
}

; An argument that is not in an aggregate has no register left either, and
; an extended-precision one is two slots.
define double @none_left([8 x float] %h, double %d, x86_fp80 %l) {
; CHECK-LABEL: none_left:
; CHECK:       st8 [r15] = r39
; CHECK-NEXT:  st8 [r14] = r38
; CHECK-NEXT:  setf.d f6 = r36
; CHECK-NEXT:  ldfe f7 = [r14] ;;
  %x = fptrunc x86_fp80 %l to double
  %r = fsub double %d, %x
  ret double %r
}

declare float @take_odd(double, double, double, double, double, double, double, [3 x float])
declare float @take_four(double, double, double, double, double, double, [4 x float])
declare void @variadic(i32, ...)
declare double @take_none_left([8 x float], double, x86_fp80)

define float @pass_odd([3 x float] %h) {
; CHECK-LABEL: pass_odd:
; CHECK:       mov f15 = f8 ;;
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK-NEXT:  stfs [r14] = f10
; CHECK:       getf.s r42 = f9
; CHECK:       br.call.sptk.many b0 = take_odd ;;
  %r = call float @take_odd(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, [3 x float] %h)
  ret float %r
}

define float @pass_four([4 x float] %h) {
; CHECK-LABEL: pass_four:
; CHECK:       mov f15 = f9
; CHECK-NEXT:  mov f14 = f8
; CHECK-NEXT:  getf.s r14 = f11 ;;
; CHECK-NEXT:  shl r14 = r14, 32
; CHECK-NEXT:  getf.s r15 = f10 ;;
; CHECK-NEXT:  zxt4 r15 = r15 ;;
; CHECK-NEXT:  or r42 = r15, r14
; CHECK:       br.call.sptk.many b0 = take_four ;;
  %r = call float @take_four(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, [4 x float] %h)
  ret float %r
}

; One the callee has not named is its bits in the slots and nowhere else.
define void @unnamed([3 x float] %h, [2 x double] %d) {
; CHECK-LABEL: unnamed:
; CHECK:       getf.s r14 = f9 ;;
; CHECK-NEXT:  shl r14 = r14, 32
; CHECK-NEXT:  getf.s r15 = f8 ;;
; CHECK-NEXT:  zxt4 r15 = r15 ;;
; CHECK-NEXT:  or r36 = r15, r14
; CHECK-NEXT:  getf.s r37 = f10
; CHECK-NEXT:  getf.d r38 = f11
; CHECK-NEXT:  getf.d r39 = f12
; CHECK-NEXT:  mov r35 = 1
; CHECK-NEXT:  br.call.sptk.many b0 = variadic ;;
  call void (i32, ...) @variadic(i32 1, [3 x float] %h, [2 x double] %d)
  ret void
}

; One the callee may or may not have named, in a call to a function that
; was declared without its parameters, is in both.
define void @maybe_named([2 x double] %d, [3 x float] %h) {
; CHECK-LABEL: maybe_named:
; CHECK:       mov f6 = f9
; CHECK-NEXT:  mov f7 = f8
; CHECK:       or r35 = r15, r14
; CHECK-NEXT:  getf.s r36 = f12
; CHECK-NEXT:  getf.d r37 = f7
; CHECK-NEXT:  getf.d r38 = f6
; CHECK-NEXT:  mov f8 = f10
; CHECK-NEXT:  mov f9 = f11
; CHECK-NEXT:  mov f10 = f12
; CHECK-NEXT:  mov f11 = f7
; CHECK-NEXT:  mov f12 = f6
; CHECK-NEXT:  br.call.sptk.many b0 = variadic ;;
  call void ([3 x float], [2 x double], ...) @variadic([3 x float] %h, [2 x double] %d)
  ret void
}

define double @pass_none_left([8 x float] %h, double %d, x86_fp80 %l) {
; CHECK-LABEL: pass_none_left:
; CHECK:       mov r46 = r36 ;;
; CHECK:       ld8 r49 = [r15]
; CHECK-NEXT:  ld8 r48 = [r14]
; CHECK-NEXT:  br.call.sptk.many b0 = take_none_left ;;
  %r = call double @take_none_left([8 x float] %h, double %d, x86_fp80 %l)
  ret double %r
}
