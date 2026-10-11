; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; "r" prints a register, and r0 for the number zero. "P" prints what a memory
; operand adds to its base, which is nothing.

define void @store(ptr %p, i64 %v) {
; CHECK-LABEL: store:
; CHECK:       st8 [r32] = r33
entry:
  call void asm sideeffect "st8 $0 = ${1:r}${0:P}", "*m,rO"(ptr elementtype(i64) %p, i64 %v)
  ret void
}

define void @store_zero(ptr %p) {
; CHECK-LABEL: store_zero:
; CHECK:       st8 [r32] = r0
entry:
  call void asm sideeffect "st8 $0 = ${1:r}${0:P}", "*m,rO"(ptr elementtype(i64) %p, i64 0)
  ret void
}

define i64 @cpuid_zero() {
; CHECK-LABEL: cpuid_zero:
; CHECK:       mov r8 = cpuid[r0]
entry:
  %r = call i64 asm sideeffect "mov $0 = cpuid[${1:r}]", "=r,rO"(i64 0)
  ret i64 %r
}
