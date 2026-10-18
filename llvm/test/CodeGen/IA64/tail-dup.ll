; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; A taken branch costs more than a few instructions, so a short block that
; several blocks branch to is copied into the end of each of them, and the
; copies are not merged again. Here each of the three paths returns where it
; is; merging the two that return zero would have let one parallel compare
; branch for both conditions, but cost the other path a branch to them.

declare i32 @g()

define i32 @returns(i64 %a, i64 %b) {
; CHECK-LABEL: returns:
; CHECK:       cmp.eq p7, p6 = 5, r32
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_3 ;;
; CHECK:       cmp.eq p6, p7 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB0_4 ;;
; CHECK:       mov r8 = r0
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK:       .LBB0_3:
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB0_4:
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .endp returns
entry:
  %cmp = icmp eq i64 %a, 5
  br i1 %cmp, label %next, label %return

next:
  %cmp1 = icmp ne i64 %b, 0
  br i1 %cmp1, label %return, label %then

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 0, %entry ], [ 0, %next ]
  ret i32 %r
}
