; RUN: llc -mtriple=ia64-unknown-linux-gnu -tail-dup-placement-threshold=2 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -tail-dup-placement-threshold=2 \
; RUN:   -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; "br" has a 21-bit count of bundles for a displacement, which is 16 MiB each
; way. A branch that has further to go is "brl", and a conditional one goes
; round it.
; The generic tail duplication threshold keeps each join a block that is
; branched to; IA64's would copy it into the blocks before it.

declare void @g()

define i64 @forward(i64 %a) {
; CHECK-LABEL: forward:
; CHECK:       cmp.eq p7, p6 = 0, r32
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_1 ;;
; CHECK-NEXT:  // %bb.{{[0-9]+}}:
; CHECK-NEXT:  brl.cond.sptk.few .LBB0_2 ;;
; CHECK-NEXT:  .LBB0_1:
; CHECK:       .zero 20000000
; CHECK:       br.cond.sptk.few .LBB0_3 ;;
; CHECK-NEXT:  .LBB0_2:
; CHECK-NEXT:  mov r8 = 1 ;;
; CHECK-NEXT:  .LBB0_3:
;
; OBJ-LABEL: <forward>:
; OBJ:       brl.cond.sptk.few 0x1312d60 ;;
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %far, label %near

near:
  call void @g()
  call void asm sideeffect ".space 20000000", ""()
  br label %far

far:
  %r = phi i64 [ 1, %entry ], [ 2, %near ]
  ret i64 %r
}

define void @backward(i64 %a) {
; CHECK-LABEL: backward:
; CHECK:       .LBB1_1:
; CHECK:       .zero 20000000
; CHECK:       cmp.ltu p7, p6 = r34, r32
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB1_2 ;;
; CHECK-NEXT:  // %bb.{{[0-9]+}}:
; CHECK-NEXT:  // in Loop
; CHECK-NEXT:  brl.cond.sptk.few .LBB1_1 ;;
; CHECK-NEXT:  .LBB1_2:
;
; OBJ-LABEL: <backward>:
; OBJ:       brl.cond.sptk.few 0x[[#%x,LOOP:]] ;;
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  call void @g()
  call void asm sideeffect ".space 20000000", ""()
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, %a
  br i1 %c, label %loop, label %out

out:
  ret void
}

; Sixteen million bytes are within reach.
define void @near(i64 %a) {
; CHECK-LABEL: near:
; CHECK:       (p6) br.cond.dptk.few .LBB2_2 ;;
; CHECK-NOT:   brl
; CHECK:       .zero 16000000
; CHECK:       .LBB2_2:
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %x, label %y

x:
  call void @g()
  call void asm sideeffect ".space 16000000", ""()
  br label %y

y:
  ret void
}
