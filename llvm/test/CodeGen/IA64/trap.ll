; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A trap is a break and not a call of abort, which a library that may not
; call itself through the procedure linkage table could not afford.

declare void @llvm.trap()
declare void @llvm.debugtrap()
declare void @llvm.ubsantrap(i8)

define void @trap() {
; CHECK-LABEL: trap:
; CHECK:       break.m 0 ;;
; CHECK-NOT:   abort
; CHECK:       .endp trap
  call void @llvm.trap()
  unreachable
}

define i64 @trap_if_zero(i64 %a) {
; CHECK-LABEL: trap_if_zero:
; CHECK:       cmp.eq p6, p7 = 0, r32
; CHECK-NEXT:  (p6) br.cond.dpnt.few [[BAD:.LBB[0-9_]+]] ;;
; CHECK:       mov r8 = r32
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  [[BAD]]:
; CHECK-NEXT:  break.m 0 ;;
; CHECK-NEXT:  .endp trap_if_zero
  %z = icmp eq i64 %a, 0
  br i1 %z, label %bad, label %ok
bad:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %a
}

; The break that a debugger plants, after which the function goes on.
define void @debugtrap() {
; CHECK-LABEL: debugtrap:
; CHECK:       break.m 838860
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  call void @llvm.debugtrap()
  ret void
}

define void @ubsantrap() {
; CHECK-LABEL: ubsantrap:
; CHECK:       break.m 0 ;;
; CHECK-NOT:   abort
; CHECK:       .endp ubsantrap
  call void @llvm.ubsantrap(i8 3)
  unreachable
}
