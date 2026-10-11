; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The callee owns an argument that is passed by value and may write to it, so
; the caller hands it the address of a copy and not of the original.

%struct.s = type { i64, i64 }
%struct.big = type { [128 x i64] }

declare void @take(ptr byval(%struct.s) align 8)
declare void @take_big(ptr byval(%struct.big) align 8, i64)

define void @small(ptr %p) {
; CHECK-LABEL: small:
; CHECK:       adds r12 = -16, r12
; CHECK:       adds r36 = 16, r12 ;;
; CHECK:       ld8 r14 = [r32] ;;
; CHECK-NEXT:  st8 [r36] = r14
; CHECK:       br.call.sptk.many b0 = take ;;
  call void @take(ptr byval(%struct.s) align 8 %p)
  ret void
}

; A large one is copied by the library, before the arguments of the call are
; put where they go.
define void @large(ptr %p, i64 %x) {
; CHECK-LABEL: large:
; CHECK:       mov r{{[0-9]+}} = 1024
; CHECK:       br.call.sptk.many b0 = memcpy ;;
; CHECK:       mov r{{[0-9]+}} = r33
; CHECK:       br.call.sptk.many b0 = take_big ;;
  call void @take_big(ptr byval(%struct.big) align 8 %p, i64 %x)
  ret void
}

; The callee reads and writes what it was given the address of.
define i64 @callee(ptr byval(%struct.s) align 8 %p) {
; CHECK-LABEL: callee:
; CHECK:       ld8 r8 = [r32]
; CHECK-NEXT:  st8 [r32] = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i64, ptr %p
  store i64 0, ptr %p
  ret i64 %v
}
