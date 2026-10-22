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
; CHECK:       adds r14 = 16, r12
; CHECK-NEXT:  fma.s f7 = f8, f1, f9 ;;
; CHECK-NEXT:  ldfs f6 = [r14] ;;
; CHECK-NEXT:  fma.s f8 = f7, f1, f6
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
; CHECK:       shr.u r14 = r39, 32
; CHECK-NEXT:  setf.s f6 = r39 ;;
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
; CHECK:       setf.s f6 = r39
; CHECK-NEXT:  adds r14 = 16, r12 ;;
; CHECK:       ldfs f7 = [r14]
; CHECK-NEXT:  fma.s f6 = f15, f1, f6 ;;
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
; CHECK:       setf.d f7 = r36 ;;
; CHECK:       st8 [r14] = r38 ;;
; CHECK-NEXT:  st8 [r15] = r39
; CHECK-NEXT:  ldfe f6 = [r14] ;;
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
; CHECK:       mov f6 = f10
; CHECK:       adds [[A:r[0-9]+]] = 16, r12
; CHECK:       getf.s r42 = f9
; CHECK:       mov f15 = f8
; CHECK:       stfs [[[A]]] = f6
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       br.call.sptk.many b0 = take_odd ;;
  %r = call float @take_odd(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, [3 x float] %h)
  ret float %r
}

define float @pass_four([4 x float] %h) {
; CHECK-LABEL: pass_four:
; CHECK:       getf.s r14 = f11
; CHECK:       mov f15 = f9
; CHECK:       getf.s r15 = f10
; CHECK:       mov f14 = f8
; CHECK:       mov f8 = f1
; CHECK:       shl r14 = r14, 32
; CHECK-NEXT:  zxt4 r15 = r15 ;;
; CHECK-NEXT:  or r42 = r15, r14
; CHECK:       br.call.sptk.many b0 = take_four ;;
  %r = call float @take_four(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, [4 x float] %h)
  ret float %r
}

; One the callee has not named is its bits in the slots and nowhere else.
define void @unnamed([3 x float] %h, [2 x double] %d) {
; CHECK-LABEL: unnamed:
; CHECK:       getf.s r14 = f9
; CHECK-NEXT:  mov r35 = 1
; CHECK-NEXT:  getf.s r15 = f8
; CHECK-NEXT:  getf.s r37 = f10
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  getf.d r38 = f11
; CHECK-NEXT:  getf.d r39 = f12 ;;
; CHECK-NEXT:  shl r14 = r14, 32
; CHECK-NEXT:  zxt4 r15 = r15 ;;
; CHECK-NEXT:  or r36 = r15, r14
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = variadic ;;
  call void (i32, ...) @variadic(i32 1, [3 x float] %h, [2 x double] %d)
  ret void
}

; One the callee may or may not have named, in a call to a function that
; was declared without its parameters, is in both.
define void @maybe_named([2 x double] %d, [3 x float] %h) {
; CHECK-LABEL: maybe_named:
; CHECK:       mov f7 = f9
; CHECK:       mov f6 = f8
; CHECK-NEXT:  getf.s r36 = f12
; CHECK-NEXT:  getf.d r37 = f8
; CHECK-NEXT:  mov f8 = f10
; CHECK-NEXT:  mov f10 = f12
; CHECK-NEXT:  getf.d r38 = f9
; CHECK-NEXT:  mov f9 = f11 ;;
; CHECK-NEXT:  mov f11 = f6
; CHECK-NEXT:  shl r14 = r14, 32
; CHECK-NEXT:  mov f12 = f7
; CHECK:       or r35 = r15, r14
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = variadic ;;
  call void ([3 x float], [2 x double], ...) @variadic([3 x float] %h, [2 x double] %d)
  ret void
}

define double @pass_none_left([8 x float] %h, double %d, x86_fp80 %l) {
; CHECK-LABEL: pass_none_left:
; CHECK:       adds [[A:r[0-9]+]] = 16, r12
; CHECK:       mov r46 = r36
; CHECK:       or [[B:r[0-9]+]] = 8, [[A]]
; CHECK:       stfe [[[A]]] = f6
; CHECK-NEXT:  ld8 r49 = [[[B]]]
; CHECK-NEXT:  ld8 r48 = [[[A]]]
; CHECK-NEXT:  .save rp, r41
; CHECK-NEXT:  mov r41 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_none_left ;;
  %r = call double @take_none_left([8 x float] %h, double %d, x86_fp80 %l)
  ret double %r
}

; An aggregate that was declared to be aligned to sixteen bytes begins at an
; even slot, and is a multiple of sixteen bytes whatever its members come to,
; so the argument after it is two slots on.

define i64 @aligned_double(i64 %x, [2 x double] alignstack(16) %a, i64 %y) {
; CHECK-LABEL: aligned_double:
; CHECK:       mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 %y
}

define i64 @aligned_single(i64 %x, [2 x float] alignstack(16) %a, i64 %y) {
; CHECK-LABEL: aligned_single:
; CHECK:       mov r8 = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 %y
}

; The registers are still the next of f8 to f15.
define double @aligned_member(i64 %x, [2 x double] alignstack(16) %a) {
; CHECK-LABEL: aligned_member:
; CHECK:       mov f8 = f9
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %b = extractvalue [2 x double] %a, 1
  ret double %b
}

; The slot that is passed over may be the last of the registers.
define double @aligned_memory(i64, i64, i64, i64, i64, i64, i64,
                              [2 x double] alignstack(16) %a) {
; CHECK-LABEL: aligned_memory:
; CHECK:       adds r14 = 24, r12 ;;
; CHECK-NEXT:  ldfd f8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %b = extractvalue [2 x double] %a, 1
  ret double %b
}

declare void @take_aligned(i64, [2 x double] alignstack(16), i64)

define void @pass_aligned([2 x double] %a) {
; CHECK-LABEL: pass_aligned:
; CHECK:       mov r39 = 2
; CHECK:       br.call.sptk.many b0 = take_aligned ;;
  call void @take_aligned(i64 1, [2 x double] alignstack(16) %a, i64 2)
  ret void
}

; An aggregate of extended-precision members that was declared to be aligned
; to less than they are begins at whichever slot is next.
define x86_fp80 @unaligned(i64 %x, [1 x x86_fp80] alignstack(8) %a, i64 %y) {
; CHECK-LABEL: unaligned:
; CHECK:       setf.sig f6 = r35 ;;
; CHECK-NEXT:  fcvt.xf f6 = f6 ;;
; CHECK-NEXT:  fma f8 = f8, f1, f6
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = extractvalue [1 x x86_fp80] %a, 0
  %f = sitofp i64 %y to x86_fp80
  %r = fadd x86_fp80 %v, %f
  ret x86_fp80 %r
}

; That may be the last of the registers, and the second half of the member is
; then the first slot of memory.
define i64 @unaligned_last(i64, i64, i64, i64, i64, i64, i64,
                           [1 x x86_fp80] alignstack(8) %a, i64 %y) {
; CHECK-LABEL: unaligned_last:
; CHECK:       adds r14 = 24, r12 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret i64 %y
}

; With no floating-point register left it is its bits in the two.
define x86_fp80 @unaligned_bits([8 x float] %h, i64, i64, i64,
                                [1 x x86_fp80] alignstack(8) %a) {
; CHECK-LABEL: unaligned_bits:
; CHECK:       adds r15 = 16, r12 ;;
; CHECK-NEXT:  or r16 = 8, r14
; CHECK-NEXT:  ld8 r15 = [r15]
; CHECK:       st8 [r{{[0-9]+}}] = r39
; CHECK:       st8 [r{{[0-9]+}}] = r15
; CHECK:       ldfe f8 = [r{{[0-9]+}}]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = extractvalue [1 x x86_fp80] %a, 0
  ret x86_fp80 %v
}

; One that is all in memory is not at an address that an extended-precision
; value can be loaded from, and is copied to one that is.
define x86_fp80 @unaligned_memory(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                                  [1 x x86_fp80] alignstack(8) %a) {
; CHECK-LABEL: unaligned_memory:
; CHECK:       adds r14 = 24, r12
; CHECK-NOT:   ldfe {{.*}}[r14]
; CHECK:       ld8 r15 = [r14]
; CHECK:       st8 [r16] = r15 ;;
; CHECK-NEXT:  st2 [r17] = r14
; CHECK-NEXT:  ldfe f8 = [r16]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = extractvalue [1 x x86_fp80] %a, 0
  ret x86_fp80 %v
}

declare void @take_unaligned(i64, i64, i64, i64, i64, i64, i64,
                             [1 x x86_fp80] alignstack(8), i64)
declare void @take_unaligned_variable(i64, ...)
declare void @take_unaligned_memory(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                                    [1 x x86_fp80] alignstack(8))

define void @pass_unaligned([1 x x86_fp80] %a) {
; CHECK-LABEL: pass_unaligned:
; CHECK:       adds r14 = 24, r12
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r15 = 9
; CHECK:       st8 [r14] = r15
; CHECK-NOT:   f8
; CHECK:       br.call.sptk.many b0 = take_unaligned ;;
  call void @take_unaligned(i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6,
                            [1 x x86_fp80] alignstack(8) %a, i64 9)
  ret void
}

define void @pass_unaligned_variable([1 x x86_fp80] %a) {
; CHECK-LABEL: pass_unaligned_variable:
; CHECK:       adds r14 = 32, r12
; CHECK:       adds r17 = 16, r12
; CHECK:       stfe [r14] = f8
; CHECK-NEXT:  or r15 = 8, r14
; CHECK:       ld8 r16 = [r15]
; CHECK-NEXT:  ld8 r42 = [r14] ;;
; CHECK-NEXT:  st8 [r17] = r16
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_unaligned_variable ;;
  call void (i64, ...) @take_unaligned_variable(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6,
      [1 x x86_fp80] alignstack(8) %a)
  ret void
}

define void @pass_unaligned_memory([1 x x86_fp80] %a) {
; CHECK-LABEL: pass_unaligned_memory:
; CHECK:       adds r14 = 48, r12
; CHECK:       adds [[A:r[0-9]+]] = 24, r12
; CHECK:       stfe [r14] = f8
; CHECK-NOT:   stfe
; CHECK:       ld8 r14 = [r14]
; CHECK:       st8 [[[A]]] = r14
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_unaligned_memory ;;
  call void @take_unaligned_memory(i64 0, i64 1, i64 2, i64 3, i64 4, i64 5,
                                   i64 6, i64 7, i64 8,
                                   [1 x x86_fp80] alignstack(8) %a)
  ret void
}

; A callee that takes a variable number of arguments, or was declared
; without its parameters, may have named a member and has it in the slot as
; well as in its register. Not the member that had the last register when
; the other half of its slot has none: a callee that has named them looks
; for the second in the low half of the slot, which is then all there is in
; it.
declare float @take_odd_variadic(double, double, double, double, double, double, double, [3 x float], ...)

define float @pass_odd_variadic([3 x float] %h) {
; CHECK-LABEL: pass_odd_variadic:
; CHECK-NOT:   getf.s {{r[0-9]+}} = f8
; CHECK:       getf.s r42 = f9
; CHECK-NOT:   getf.s {{r[0-9]+}} = f8
; CHECK:       mov f15 = f8
; CHECK-NOT:   r42
; CHECK:       br.call.sptk.many b0 = take_odd_variadic ;;
  %r = call float (double, double, double, double, double, double, double, [3 x float], ...) @take_odd_variadic(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, [3 x float] %h)
  ret float %r
}

; The one that has the last register and a slot to itself is in both.
define float @pass_one_variadic([1 x float] %h) {
; CHECK-LABEL: pass_one_variadic:
; CHECK:       mov f15 = f8
; CHECK-NEXT:  getf.s r42 = f8
; CHECK:       br.call.sptk.many b0 = take_odd_variadic ;;
  %r = call float (double, double, double, double, double, double, double, [1 x float], ...) @take_odd_variadic(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, [1 x float] %h)
  ret float %r
}
