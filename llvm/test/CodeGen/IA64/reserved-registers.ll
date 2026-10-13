; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,FREE
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -mattr=+reserve-r14,+reserve-r15,+reserve-f6,+reserve-f7 \
; RUN:   -mattr=+reserve-b1,+reserve-b2,+reserve-b3,+reserve-b4 < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,KEPT

; A register that is reserved holds no value of the function's.

define i64 @general(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: general:
; FREE:          add r14 = r32, r33 ;;
; FREE-NEXT:     andcm r8 = r34, r14
; KEPT:          add r16 = r32, r33 ;;
; KEPT-NEXT:     andcm r8 = r34, r16
  %sum = add i64 %a, %b
  %not = xor i64 %sum, -1
  %and = and i64 %c, %not
  ret i64 %and
}

define double @floating(double %a, double %b, double %c) {
; CHECK-LABEL: floating:
; FREE:          fma.d f6 = f8, f1, f9 ;;
; FREE-NEXT:     fma.d f8 = f6, f6, f0
; KEPT-NOT:      f6
; KEPT-NOT:      f7
; KEPT:          br.ret.sptk.many b0
  %sum = fadd double %a, %b
  %product = fmul double %sum, %sum
  ret double %product
}

; A branch register that is reserved is not the function's to save when the
; source changes it in assembly, and is not given to an operand of the
; assembly that wants a branch register.

define void @branch_changed() {
; CHECK-LABEL: branch_changed:
; FREE:          .spillreg b1, r33
; FREE-NEXT:     mov r33 = b1
; FREE:          mov b1 = r33
; KEPT-NOT:      b1
; KEPT:          br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{b1}"()
  ret void
}

define i64 @branch_operand(i64 %a) {
; CHECK-LABEL: branch_operand:
; FREE:          mov b1 = r32
; KEPT:          mov b5 = r32
  %r = call i64 asm "mov $0 = $1", "=b,r,~{b6},~{b7}"(i64 %a)
  ret i64 %r
}
