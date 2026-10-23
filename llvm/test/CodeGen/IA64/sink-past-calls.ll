; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-sink-fr-defs=0 < %s | FileCheck %s --check-prefix=NOSINK

declare double @f(double)

; Doubles passed in a GR and on the stack and used only after a call are
; moved into FRs after it, so the function keeps no FR across the call and
; saves none.
define double @after_call(double %x, [8 x i64] %a) {
; CHECK-LABEL: after_call:
; CHECK-NOT:     stf.spill
; CHECK:         br.call.sptk.many b0 = f ;;
; CHECK-NEXT:    setf.d f6 = r34
; CHECK-NEXT:    ldfd f7 = [r33]
; CHECK-NOT:     ldf.fill
; CHECK:         br.ret.sptk.many b0 ;;
; NOSINK-LABEL: after_call:
; NOSINK:        stf.spill [r2] = f3
; NOSINK:        stf.spill [r3] = f2
; NOSINK:        setf.d f2 = r34
; NOSINK:        ldfd f3 = [r14]
; NOSINK:        br.call.sptk.many b0 = f ;;
  %a1 = extractvalue [8 x i64] %a, 1
  %d1 = bitcast i64 %a1 to double
  %a7 = extractvalue [8 x i64] %a, 7
  %d7 = bitcast i64 %a7 to double
  %c = call double @f(double %x)
  %s = fadd double %c, %d1
  %t = fadd double %s, %d7
  ret double %t
}

; A value also used before the call stays where it is.
define double @before_call(double %x, i64 %a) {
; CHECK-LABEL: before_call:
; CHECK:         stf.spill [r2] = f2
; CHECK-NEXT:    setf.d f2 = r33
; CHECK:         fma.d f8 = f8, f2, f0
; CHECK-NEXT:    br.call.sptk.many b0 = f ;;
  %d = bitcast i64 %a to double
  %m = fmul double %x, %d
  %c = call double @f(double %m)
  %s = fadd double %c, %d
  ret double %s
}
