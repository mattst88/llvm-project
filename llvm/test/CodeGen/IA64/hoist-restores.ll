; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-early-restores=false < %s | FileCheck %s --check-prefix=OFF
; The option is not named after the pass, so tools that make an option of
; every pass name start.
; RUN: opt -mtriple=ia64-unknown-linux-gnu -disable-output < %s

declare i64 @g(i64)

; The return of zero has one instruction and two restores that only port I0
; executes, so they go to the block before it, in the group of its compare.
define i64 @after_call(i64 %a) {
; CHECK-LABEL: after_call:
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  cmp.eq p6, p7 = 0, r8
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB0_2{{$}}
; CHECK:       .LBB0_2:
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; OFF-LABEL:   after_call:
; OFF:         .LBB0_2:
; OFF-NEXT:    mov r8 = r0
; OFF-NEXT:    mov b0 = r35
; OFF-NEXT:    mov ar.pfs = r34
; OFF-NEXT:    br.ret.sptk.many b0 ;;
entry:
  %x = call i64 @g(i64 %a)
  %c = icmp eq i64 %x, 0
  br i1 %c, label %ret, label %more
more:
  %y = call i64 @g(i64 %x)
  br label %ret
ret:
  %r = phi i64 [ 0, %entry ], [ %y, %more ]
  ret i64 %r
}

; Nothing has written b0 or ar.pfs since they were saved, so the early
; return needs no restores at all.
define i64 @early(i64 %a, i64 %b) {
; CHECK-LABEL: early:
; CHECK:       mov r36 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  cmp.eq p6, p7 = 0, r32
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB1_2{{$}}
; CHECK:       .LBB1_2:
; CHECK-NEXT:  mov r8 = 7
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; OFF-LABEL:   early:
; OFF:         .LBB1_2:
; OFF-NEXT:    mov r8 = 7
; OFF-NEXT:    mov b0 = r36
; OFF-NEXT:    mov ar.pfs = r35
; OFF-NEXT:    br.ret.sptk.many b0 ;;
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %ret, label %call
call:
  %y = call i64 @g(i64 %b)
  br label %ret
ret:
  %r = phi i64 [ 7, %entry ], [ %y, %call ]
  ret i64 %r
}

; The two loads take long enough for both restores to issue beside them, so
; the restores stay.
define i64 @roomy(i64 %a, ptr %p) {
; CHECK-LABEL: roomy:
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  cmp.eq p6, p7 = 0, r8
; CHECK:       .LBB2_2:
; CHECK:       ld8 r14 = [r33]
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  mov b0 = r36 ;;
; CHECK-NEXT:  ld8 r14 = [r14]
; CHECK-NEXT:  mov ar.pfs = r35 ;;
entry:
  %x = call i64 @g(i64 %a)
  %c = icmp eq i64 %x, 0
  br i1 %c, label %ret, label %more
more:
  %y = call i64 @g(i64 %x)
  br label %ret
ret:
  %r = phi i64 [ 0, %entry ], [ %y, %more ]
  %l1 = load i64, ptr %p
  %q = inttoptr i64 %l1 to ptr
  %l2 = load i64, ptr %q
  %s = add i64 %l2, %r
  ret i64 %s
}

; One way to the return is from the loop, where the restores would run on
; every iteration.
define i64 @in_loop(i64 %n) {
; CHECK-LABEL: in_loop:
; CHECK:       .LBB3_1:
; CHECK-NOT:   mov b0 =
; CHECK:       .LBB3_4:
; CHECK-NEXT:  mov r8 = r33
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.lc = r37
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %latch ]
  %x = call i64 @g(i64 %i)
  %c = icmp eq i64 %x, 0
  br i1 %c, label %ret, label %latch
latch:
  %i1 = add i64 %i, 1
  %d = icmp ult i64 %i1, %n
  br i1 %d, label %loop, label %ret
ret:
  %r = phi i64 [ %i, %loop ], [ %n, %latch ]
  ret i64 %r
}

; Only b0 and ar.pfs move. The first block that the restore of ar.lc would
; move to also leads to the counted loop.
define void @counted(ptr %p, i64 %n) {
; CHECK-LABEL: counted:
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK:       br.cloop.sptk.few .LBB4_2{{$}}
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.pfs = r35{{$}}
; CHECK-NEXT:  .LBB4_4:
; CHECK-NEXT:  mov ar.lc = r37
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  %x = call i64 @g(i64 %n)
  %c = icmp eq i64 %x, 0
  br i1 %c, label %ret, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %a = getelementptr i64, ptr %p, i64 %i
  store i64 %i, ptr %a
  %i1 = add i64 %i, 1
  %d = icmp ult i64 %i1, %x
  br i1 %d, label %loop, label %after
after:
  %y = call i64 @g(i64 %x)
  br label %ret
ret:
  ret void
}

; Both returns of zero lose their restores. The block that tests %b is
; entered only after the restores in the block before it, so it needs none
; of its own.
define i64 @chain(i64 %a, i64 %b) {
; CHECK-LABEL: chain:
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r34
; CHECK-NEXT:  mov b0 = r36
; CHECK-NEXT:  mov ar.pfs = r35
; CHECK-NEXT:  cmp.eq p7, p6 = 5, r8
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB5_3 ;;
; CHECK-NEXT:  // %bb.1:
; CHECK-NEXT:  cmp.eq p6, p7 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB5_4{{$}}
; CHECK-NEXT:  // %bb.2:
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB5_3:
; CHECK-NEXT:  mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
entry:
  %x = call i64 @g(i64 %a)
  %c = icmp eq i64 %x, 5
  br i1 %c, label %next, label %ret
next:
  %c1 = icmp ne i64 %b, 0
  br i1 %c1, label %ret, label %then
then:
  %y = call i64 @g(i64 %b)
  br label %ret
ret:
  %r = phi i64 [ 0, %entry ], [ 0, %next ], [ %y, %then ]
  ret i64 %r
}
