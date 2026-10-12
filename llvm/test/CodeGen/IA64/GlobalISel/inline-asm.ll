; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -verify-machineinstrs \
; RUN:   < %s 2>&1 | FileCheck %s

; An assembly statement that names general registers, memory or nothing at
; all is selected here. One that wants a floating-point or a branch register
; is SelectionDAG's: the value would have to change its form on the way in,
; and nothing here knows how.

; CHECK: warning: Instruction selection used fallback path for floating
; CHECK: warning: Instruction selection used fallback path for branch
; CHECK-NOT: warning:

define void @barrier() {
; CHECK-LABEL: barrier:
; CHECK:       br.ret.sptk.many b0 ;;
  call void asm sideeffect "", "~{memory}"()
  ret void
}

define i64 @registers(i64 %a, i64 %b) {
; CHECK-LABEL: registers:
; CHECK:       add r8 = r32, r33
  %r = call i64 asm "add $0 = $1, $2", "=r,r,r"(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @named(i64 %a) {
; CHECK-LABEL: named:
; CHECK:       mov r14 = r32
; CHECK:       mov r15 = r14
; CHECK:       mov r8 = r15
  %r = call i64 asm "mov $0 = $1", "={r15},{r14}"(i64 %a)
  ret i64 %r
}

define void @memory(ptr %p) {
; CHECK-LABEL: memory:
; CHECK:       lfetch [r32]
  call void asm sideeffect "lfetch $0", "*m"(ptr elementtype(i8) %p)
  ret void
}

define double @floating(double %a) {
; CHECK-LABEL: floating:
; CHECK:       fmerge.ns f8 = f8, f8
  %r = call double asm "fneg $0 = $1", "=f,f"(double %a)
  ret double %r
}

define void @branch(i64 %a) {
; CHECK-LABEL: branch:
; CHECK:       mov b6 = r32
  call void asm sideeffect "nop.i 0 // $0", "b"(i64 %a)
  ret void
}
