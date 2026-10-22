; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-rotating-min-stages=2 < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=MIN
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-rotating-max-stages=1 -pass-remarks-analysis=pipeliner \
; RUN:   -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=CAP
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=CYC

; A pipelined loop is a single kernel that br.ctop runs ar.lc + ar.ec times,
; one stage predicate per stage turning on as the loop fills and off as it
; drains. Each value lives in a rotating register, so the add in stage 1
; reads the load from stage 0 one register up, and the sum from the
; iteration before one register up from where it writes it. Its final value
; is read one register up as well, before clrrrb renames the registers back.
; The rotating region is a multiple of eight registers, and a predicate
; rotation clobbers p16-p63, so pr is saved.
define i64 @sum(ptr %a, i64 %n) {
; CHECK-LABEL: sum:
; CHECK:       alloc r{{[0-9]+}} = ar.pfs, 2, {{[0-9]+}}, 0, 8
; CHECK:       mov [[PR:r[0-9]+]] = pr
; CHECK:       mov [[A:r[0-9]+]] = r32
; CHECK:       mov r32 = r0
; CHECK:       mov ar.ec = 2
; CHECK-NEXT:  mov pr.rot = 65536 ;;
; CHECK-NEXT:  .LBB0_[[LOOP:[0-9]+]]:
; CHECK:       (p17) add r32 = r33, r35
; CHECK-NEXT:  (p16) ld8 r34 = [[[A]]], 8
; CHECK-NEXT:  br.ctop.sptk.few .LBB0_[[LOOP]] ;;
; CHECK:       mov r8 = r33
; CHECK-NEXT:  clrrrb ;;
; CHECK:       mov pr = [[PR]], 65598
; CHECK-NEXT:  br.ret.sptk.many b0
;
; By default, a loop needs three stages to rotate: a two-stage one drains in
; an extra pass of its kernel, which costs more than the default expander's
; copy of it when the loop runs only an iteration or two.
; MIN-LABEL:   sum:
; MIN-NOT:     br.ctop
; MIN-LABEL:   axpy:
; MIN:         br.ctop
;
; -ia64-rotating-max-stages caps the stages the scheduler may use.
; CAP:         Schedule found with Initiation Interval: 1, MaxStageCount: 1
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr i64, ptr %a, i64 %i
  %v = load i64, ptr %p
  %s.next = add i64 %s, %v
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  %r = phi i64 [ 0, %entry ], [ %s.next, %loop ]
  ret i64 %r
}

; Floating-point values rotate in f32 up, so the fma reads each load as many
; registers up as stages later it runs, and the store the fma's result four
; stages after it. Eleven iterations overlap to cover the load latency, more
; stages than the default expander allows.
define void @axpy(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
; CHECK-LABEL: axpy:
; CHECK:       mov ar.ec = 11
; CHECK-NEXT:  mov pr.rot = 65536 ;;
; CHECK-NEXT:  .LBB1_[[LOOP:[0-9]+]]:
; CHECK:       (p26) stfd [r32] = f36, 8
; CHECK-NEXT:  (p16) ldfd f37 = [r33], 8
; CHECK-NEXT:  (p22) fma.d f32 = f43, f8, f50
; CHECK-NEXT:  (p16) ldfd f44 = [r14], 8
; CHECK-NEXT:  br.ctop.sptk.few .LBB1_[[LOOP]] ;;
; CHECK:       clrrrb ;;
;
; Kept to two stages, it starts an iteration every six cycles, not every one.
; CAP:         Schedule found with Initiation Interval: 6, MaxStageCount: 1
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr double, ptr %x, i64 %i
  %py = getelementptr double, ptr %y, i64 %i
  %vx = load double, ptr %px
  %vy = load double, ptr %py
  %m = fmul contract double %vx, %a
  %r = fadd contract double %m, %vy
  store double %r, ptr %py
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; %k arrives in r36. The loop rotates only r32-r35, but alloc rounds the
; rotating region up to eight registers, so br.ctop renames r36 as well. %k
; is copied out before the loop.
define i64 @live(ptr noalias %d, ptr noalias %s, i64 %n, i64 %u, i64 %k) {
; CHECK-LABEL: live:
; CHECK:       alloc r{{[0-9]+}} = ar.pfs, 5, {{[0-9]+}}, 0, 8
; CHECK:       mov r8 = r36
; CHECK:       br.ctop.sptk.few
; CHECK-NOT:   r36
; CHECK:       br.ret.sptk.many b0
;
; CAP:         Schedule found with Initiation Interval: 2, MaxStageCount: 1
entry:
  %z = icmp eq i64 %n, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %ps = getelementptr i64, ptr %s, i64 %i
  %pd = getelementptr i64, ptr %d, i64 %i
  %v = load i64, ptr %ps
  %w = add i64 %v, 1
  store i64 %w, ptr %pd
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i64 %k
}

; A loop that runs at most eight iterations, like the remainder of an
; unrolled loop, would spend more passes of its kernel filling and draining
; than running, so it is not rotated, and the scheduler keeps to the stages
; the default expander allows.
define void @short(ptr noalias %y, ptr noalias %x, double %a, i64 %n) {
; CHECK-LABEL: short:
; CHECK-NOT:   br.ctop
; CHECK:       br.ret.sptk.many b0
;
; CAP:         Schedule found with Initiation Interval: {{[0-9]+}}, MaxStageCount: {{[0-2]}}{{$}}
entry:
  %m = and i64 %n, 7
  %z = icmp eq i64 %m, 0
  br i1 %z, label %exit, label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr double, ptr %x, i64 %i
  %py = getelementptr double, ptr %y, i64 %i
  %vx = load double, ptr %px
  %vy = load double, ptr %py
  %m.x = fmul contract double %vx, %a
  %r = fadd contract double %m.x, %vy
  store double %r, ptr %py
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %m
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; The last step of a division writes its quotient over the reciprocal it
; refines, which has rotated by then, so the quotient is written to the
; register the reciprocal has rotated into rather than to one of its own
; that would start each pass as a copy of it.
define double @recip(ptr noalias %u, i64 %n) {
; CHECK-LABEL: recip:
; CHECK:       .LBB{{[0-9]+}}_[[LOOP:[0-9]+]]:
; CHECK-NOT:   ) mov f
; CHECK:       br.ctop.sptk.few .LBB{{[0-9]+}}_[[LOOP]] ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %loop ]
  %i.next = add nuw nsw i64 %i, 1
  %c = sitofp i64 %i.next to double
  %r = fdiv double 1.0, %c
  %p = getelementptr inbounds double, ptr %u, i64 %i
  %x = load double, ptr %p
  %s.next = call double @llvm.fmuladd.f64(double %r, double %x, double %s)
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret double %s.next
}

; When the quotient is read only in its own stage it has no register of its
; own to rotate into, and is written over the reciprocal in place, not to a
; static register that each pass copies the reciprocal into.
define double @recip_sum(ptr noalias %u, i32 %k, i64 %n) {
; The kernel issues in six groups of two F-unit instructions each, the
; cycles of its schedule. A stop only where a group reads what it wrote
; leaves four, one of six F-unit instructions, which take seven cycles.
; CYC-LABEL: recip_sum:
; CYC:         [[LOOP:.LBB[0-9]+_[0-9]+]]:
; CYC-COUNT-5: ;;{{$}}
; CYC-NOT:     ;;{{$}}
; CYC:         br.ctop.sptk.few [[LOOP]] ;;
; MIN-LABEL: recip_sum:
; MIN:       .LBB{{[0-9]+}}_[[LOOP:[0-9]+]]:
; MIN-NOT:   mov f
; MIN:       ) fma.d [[Q:f[0-9]+]] = f{{[0-9]+}}, f{{[0-9]+}}, f{{[0-9]+}}
; MIN-NOT:   mov f
; MIN:       ) fma.d f{{[0-9]+}} = [[Q]], f
; MIN-NOT:   mov f
; MIN:       br.ctop.sptk.few .LBB{{[0-9]+}}_[[LOOP]] ;;
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %j = phi i32 [ 0, %entry ], [ %j.next, %loop ]
  %s = phi double [ 0.0, %entry ], [ %s.next, %loop ]
  %a = add nuw nsw i32 %j, %k
  %t = trunc i64 %i to i32
  %m = mul i32 %a, %t
  %h = lshr i32 %m, 1
  %i.next = add nuw nsw i64 %i, 1
  %j.next = add nuw nsw i32 %j, 1
  %t1 = trunc i64 %i.next to i32
  %d = add nuw i32 %h, %t1
  %c = sitofp i32 %d to double
  %r = fdiv double 1.0, %c
  %p = getelementptr inbounds double, ptr %u, i64 %i
  %x = load double, ptr %p
  %s.next = call double @llvm.fmuladd.f64(double %r, double %x, double %s)
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret double %s.next
}

declare double @llvm.fmuladd.f64(double, double, double)

; A division reads its dividend for frcpa and again stages later for the
; remainder. A dividend of 1.0 is a copy of f1, which both read directly
; rather than through a rotating register that each pass copies f1 into.
define void @recip_fill(ptr %d, i1 %again, double %x) {
; CHECK-LABEL: recip_fill:
; CHECK:       .LBB{{[0-9]+}}_[[LOOP:[0-9]+]]:
; CHECK-NOT:   ) mov f
; CHECK:       frcpa f{{[0-9]+}}, p{{[0-9]+}} = f1, [[X:f[0-9]+]]
; CHECK-NOT:   ) mov f
; CHECK:       fnma.s1 f{{[0-9]+}} = [[X]], f{{[0-9]+}}, f1
; CHECK-NOT:   ) mov f
; CHECK:       br.ctop.sptk.few .LBB{{[0-9]+}}_[[LOOP]] ;;
entry:
  br label %outer
outer:
  br i1 %again, label %loop, label %exit
loop:
  %i = phi i64 [ %i.next, %loop ], [ 0, %outer ]
  %r = fdiv double 1.0, %x
  %pd = getelementptr inbounds double, ptr %d, i64 %i
  store double %r, ptr %pd
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, 0
  br i1 %done, label %outer, label %loop
exit:
  ret void
}
