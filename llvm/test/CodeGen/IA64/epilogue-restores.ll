; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; Restoring ar.lc, b0 and ar.pfs takes the first I unit, so the scheduler
; spreads the three over the groups of the epilogue rather than leaving them
; for the last.

declare void @g()

define void @spread(ptr %p, i64 %n, ptr %q, i64 %i) {
; CHECK-LABEL: spread:
; CHECK:       // %exit
; CHECK:       shladd r14 = r35, 3, r34
; CHECK-NEXT:  mov b0 = r38 ;;
; CHECK-NEXT:  adds r14 = 8, r14
; CHECK-NEXT:  mov ar.lc = r39 ;;
; CHECK-NEXT:  st8 [r14] = r35
; CHECK-NEXT:  mov ar.pfs = r37
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  call void @g()
  %c = icmp sgt i64 %n, 0
  br i1 %c, label %loop, label %exit

loop:
  %k = phi i64 [ 0, %entry ], [ %k1, %loop ]
  %a = getelementptr i64, ptr %p, i64 %k
  store i64 0, ptr %a
  %k1 = add i64 %k, 1
  %d = icmp eq i64 %k1, %n
  br i1 %d, label %exit, label %loop

exit:
  %b = getelementptr i64, ptr %q, i64 %i
  %b2 = getelementptr i64, ptr %b, i64 1
  store i64 %i, ptr %b2
  ret void
}
