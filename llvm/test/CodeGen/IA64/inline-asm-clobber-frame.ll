; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; A function that makes no call has no need to put back ar.pfs or the return
; link, unless what the source wrote in assembly says that it changes them: a
; system call is made that way, and the kernel returns with ar.pfs changed.

define i64 @pfs(i64 %a) {
; CHECK-LABEL: pfs:
; CHECK:       alloc r33 = ar.pfs, 1, 1, 0, 0
; CHECK:       break
; CHECK-NOT:   b0
; CHECK:       mov ar.pfs = r33
; CHECK-NEXT:  br.ret.sptk.many b0
  %r = call i64 asm sideeffect "break 0x100000", "={r8},{r15},~{ar.pfs}"(i64 %a)
  ret i64 %r
}

define void @rp() {
; CHECK-LABEL: rp:
; CHECK:       alloc r32 = ar.pfs, 0, 2, 0, 0
; CHECK:       mov r33 = b0
; CHECK:       mov ar.pfs = r32
; CHECK-NEXT:  mov b0 = r33
; CHECK-NEXT:  br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{rp}"()
  ret void
}

define void @neither() {
; CHECK-LABEL: neither:
; CHECK-NOT:   alloc
; CHECK-NOT:   ar.pfs
; CHECK:       br.ret.sptk.many b0
  call void asm sideeffect "nop 0", "~{r14}"()
  ret void
}
