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

; Block placement copies the epilogue into the blocks that branch to it,
; after they were scheduled. They are scheduled again, so that the restores
; move up among the instructions before them.
define i64 @copied(ptr %p, ptr %q, i64 %c) {
; CHECK-LABEL: copied:
; CHECK:       (p6) br.cond.dpnt.few .LBB1_2 ;;
; CHECK-NEXT:  // %b
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  ld8 r14 = [r33]
; CHECK-NEXT:  mov b0 = r37 ;;
; CHECK-NEXT:  add r8 = r14, r34
; CHECK-NEXT:  mov ar.pfs = r36
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB1_2:
; CHECK:       ld8 r14 = [r32]
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  ld8 r15 = [r33]
; CHECK-NEXT:  mov ar.pfs = r36 ;;
; CHECK-NEXT:  mov b0 = r37
; CHECK-NEXT:  cmp.lt p6, p7 = r14, r15 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  call void @g()
  %t = icmp eq i64 %c, 0
  br i1 %t, label %a, label %b

a:
  %x = load i64, ptr %p
  %y = load i64, ptr %q
  %lt = icmp slt i64 %x, %y
  %r = zext i1 %lt to i64
  br label %ret

b:
  %z = load i64, ptr %q
  %s = add i64 %z, %c
  br label %ret

ret:
  %v = phi i64 [ %r, %a ], [ %s, %b ]
  ret i64 %v
}
