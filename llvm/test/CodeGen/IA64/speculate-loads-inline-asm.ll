; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An asm that clobbers memory defines the address of a load in a loop. IA-64
; load speculation looks through its definition as it would through a
; load's, and must not read its first operand, the text, as a register.

; CHECK-LABEL: f:
; CHECK:       .LBB0_1:
; CHECK:       mov r15 = ar.itc
; CHECK:       ld8 r15 = [r15]
; CHECK:       br.cond.dptk.few .LBB0_1
define i64 @f(i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s1, %loop ]
  %p = call ptr asm sideeffect "mov $0 = ar.itc", "=r,~{memory}"()
  %v = load i64, ptr %p
  %s1 = add i64 %s, %v
  %i1 = add i64 %i, 1
  %c = icmp ult i64 %i1, %n
  br i1 %c, label %loop, label %exit

exit:
  ret i64 %s1
}
