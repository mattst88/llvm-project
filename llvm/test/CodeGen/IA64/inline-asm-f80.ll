; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An "f" operand takes a value of any of the three floating-point types, and
; the 80-bit one goes to the statement and comes back from it as it is.

define i64 @in(x86_fp80 %x) {
; CHECK-LABEL: in:
; CHECK: getf.exp r8 = f8
  %r = call i64 asm "getf.exp $0=$1", "=r,f"(x86_fp80 %x)
  ret i64 %r
}

define x86_fp80 @out(ptr %p) {
; CHECK-LABEL: out:
; CHECK: ldfe f8 = [r32]
  %r = call x86_fp80 asm "ldfe $0=[$1]", "=f,r"(ptr %p)
  ret x86_fp80 %r
}

define x86_fp80 @inout(x86_fp80 %x) {
; CHECK-LABEL: inout:
; CHECK: fmerge.s f8 = f0, f8
  %r = call x86_fp80 asm "fabs $0=$1", "=f,f"(x86_fp80 %x)
  ret x86_fp80 %r
}

define void @single(ptr %p, float %x) {
; CHECK-LABEL: single:
; CHECK: fma.d f6 = f8, f1, f0 ;;
; CHECK: stfs [r32] = f6
  call void asm sideeffect "stfs [$0]=$1", "r,f"(ptr %p, float %x)
  ret void
}
