; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-pipeliner=false < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs -global-isel \
; RUN:   -global-isel-abort=1 -ia64-pipeliner=false < %s | FileCheck %s

; A loop that runs a known number of times counts down in ar.lc, and
; br.cloop decrements it and branches back while it is not zero. ar.lc is
; callee-saved, so the prologue saves it and every return puts it back.
define i64 @sum(ptr %p, i64 %n) {
; CHECK-LABEL: sum:
; CHECK:       .save ar.lc, [[LC:r[0-9]+]]
; CHECK-NEXT:  mov [[LC]] = ar.lc
; CHECK:       adds [[N:r[0-9]+]] = -1, r33
; CHECK:       mov ar.lc = [[N]]{{$}}
; CHECK-NEXT:  .LBB0_2:
; CHECK-NOT:   cmp
; CHECK:       br.cloop.sptk.few .LBB0_2 ;;
; CHECK-NEXT:  // %bb.3:
; CHECK-NEXT:  mov ar.lc = [[LC]]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK:       mov ar.lc = [[LC]]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  %c = icmp sgt i64 %n, 0
  br i1 %c, label %loop, label %exit
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %q
  %s.next = add i64 %s, %v
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  %r = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  ret i64 %r
}

; A function the loop calls keeps ar.lc as it found it.
declare void @g(i64)
define void @calls(i64 %n) {
; CHECK-LABEL: calls:
; CHECK:       mov ar.lc = {{r[0-9]+}}{{$}}
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK:       br.cloop.sptk.few .LBB1_
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  call void @g(i64 %i)
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; Only the inner of two nested loops counts in ar.lc, which the outer one
; would have to save around it.
define void @nest(ptr %p, i64 %n, i64 %m) {
; CHECK-LABEL: nest:
; CHECK:       .LBB2_[[OUTER:[0-9]+]]:
; CHECK:       mov ar.lc = {{r[0-9]+}}{{$}}
; CHECK:       .LBB2_[[INNER:[0-9]+]]:
; CHECK:       br.cloop.sptk.few .LBB2_[[INNER]]{{$}}
; CHECK:       br.cond.{{.*}} .LBB2_[[OUTER]]
; CHECK-NOT:   br.cloop
; CHECK:       .endp nest
entry:
  br label %outer
outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %outer.latch ]
  br label %inner
inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %q = getelementptr i64, ptr %p, i64 %j
  store volatile i64 %i, ptr %q
  %j.next = add i64 %j, 1
  %inner.done = icmp eq i64 %j.next, %m
  br i1 %inner.done, label %outer.latch, label %inner
outer.latch:
  %i.next = add i64 %i, 1
  %outer.done = icmp eq i64 %i.next, %n
  br i1 %outer.done, label %exit, label %outer
exit:
  ret void
}

; Inline assembly may use ar.lc, so a loop containing it counts itself and
; leaves ar.lc alone.
define void @asm(i64 %n) {
; CHECK-LABEL: asm:
; CHECK-NOT:   ar.lc
; CHECK:       .endp asm
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  call void asm sideeffect "nop.m 0", ""()
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; A branch out of the loop when the count runs out is the same branch with
; its successors swapped, under however many inversions of the condition.
declare void @llvm.set.loop.iterations.i64(i64)
declare i1 @llvm.loop.decrement.i64(i64)
define void @inverted(i64 %n, ptr %p) {
; CHECK-LABEL: inverted:
; CHECK:       mov ar.lc = {{r[0-9]+}}{{$}}
; CHECK-NEXT:  .LBB4_[[LOOP:[0-9]+]]:
; CHECK:       br.cloop.sptk.few .LBB4_[[LOOP]] ;;
; CHECK-NEXT:  // %bb
; CHECK-NEXT:  mov ar.lc
entry:
  %m = add i64 %n, 1
  call void @llvm.set.loop.iterations.i64(i64 %m)
  br label %loop
loop:
  store volatile i64 0, ptr %p
  %d = call i1 @llvm.loop.decrement.i64(i64 1)
  %x = xor i1 %d, true
  br i1 %x, label %exit, label %loop
exit:
  ret void
}
