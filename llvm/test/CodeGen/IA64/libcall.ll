; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The runtime library has the usual functions under the usual names.

define void @copy(ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: copy:
; CHECK: br.call.sptk.many b0 = memcpy ;;
  call void @llvm.memcpy.p0.p0.i64(ptr %a, ptr %b, i64 %n, i1 false)
  ret void
}

define void @fill(ptr %a, i64 %n) {
; CHECK-LABEL: fill:
; CHECK: br.call.sptk.many b0 = memset ;;
  call void @llvm.memset.p0.i64(ptr %a, i8 0, i64 %n, i1 false)
  ret void
}
