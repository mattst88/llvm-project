; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s

; Up to 32 stores are emitted in place of a call to memset, memcpy or
; memmove. A 16-byte copy of unknown alignment takes 16 byte stores.

; CHECK-LABEL: move16:
; CHECK-NOT:     br.call
; CHECK-COUNT-16: st1
; CHECK-NOT:     br.call
; CHECK:         br.ret
define void @move16(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; CHECK-LABEL: copy16:
; CHECK-NOT:     br.call
; CHECK-COUNT-16: st1
; CHECK-NOT:     br.call
; CHECK:         br.ret
define void @copy16(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; CHECK-LABEL: zero256:
; CHECK-NOT:     br.call
; CHECK-COUNT-32: st8
; CHECK-NOT:     br.call
; CHECK:         br.ret
define void @zero256(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 256, i1 false)
  ret void
}

; One store more is a call.
; CHECK-LABEL: zero264:
; CHECK:         br.call.sptk.many b0 = memset
define void @zero264(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 264, i1 false)
  ret void
}

; Optimizing for size keeps the generic limit.
; CHECK-LABEL: zero64_minsize:
; CHECK:         br.call.sptk.many b0 = memset
define void @zero64_minsize(ptr %d) minsize {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 64, i1 false)
  ret void
}
