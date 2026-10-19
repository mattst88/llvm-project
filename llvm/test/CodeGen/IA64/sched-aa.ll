; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The second iteration loads y[i + 1] after the first stores y[i]. Only alias
; analysis sees that the two differ, which lets both pairs of loads issue
; before either store.
define void @add2(ptr noalias %y, ptr noalias %x, i64 %n) {
; CHECK-LABEL: add2:
; CHECK:       .LBB0_1:
; CHECK:       ld8 r16 = [r33], 16
; CHECK-NEXT:  ld8 r17 = [r32] ;;
; CHECK-NEXT:  ld8 r18 = [r15], 16
; CHECK-NEXT:  ld8 r19 = [r14]
; CHECK-NEXT:  add r16 = r16, r17 ;;
; CHECK-NEXT:  st8 [r32] = r16
; CHECK-NEXT:  adds r32 = 16, r32
; CHECK-NEXT:  add r20 = r18, r19 ;;
; CHECK-NEXT:  st8 [r14] = r20
; CHECK-NEXT:  adds r14 = 16, r14
; CHECK-NEXT:  br.cloop.sptk.few .LBB0_1 ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px0 = getelementptr inbounds i64, ptr %x, i64 %i
  %py0 = getelementptr inbounds i64, ptr %y, i64 %i
  %x0 = load i64, ptr %px0
  %y0 = load i64, ptr %py0
  %s0 = add i64 %x0, %y0
  store i64 %s0, ptr %py0
  %i1 = or disjoint i64 %i, 1
  %px1 = getelementptr inbounds i64, ptr %x, i64 %i1
  %py1 = getelementptr inbounds i64, ptr %y, i64 %i1
  %x1 = load i64, ptr %px1
  %y1 = load i64, ptr %py1
  %s1 = add i64 %x1, %y1
  store i64 %s1, ptr %py1
  %i.next = add nuw i64 %i, 2
  %c = icmp eq i64 %i.next, %n
  br i1 %c, label %exit, label %loop
exit:
  ret void
}
