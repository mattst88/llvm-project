; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; In a copy unrolled by two, the second load would wait behind the first
; store, which waits for the first load. ld.sa issues it beside the first
; load, and the increment its post-increment form made becomes an add.
define void @copy2(ptr %d, ptr %s, i64 %ds, i64 %ss, i64 %n) {
; CHECK-LABEL: copy2:
; CHECK:       .LBB0_1:
; CHECK:       ldfd f{{[0-9]+}} = [r{{[0-9]+}}], r{{[0-9]+}}
; CHECK:       ldfd.sa [[F:f[0-9]+]] = {{\[}}[[A:r[0-9]+]]]
; CHECK-NEXT:  stfd
; CHECK:       ldfd.c.clr [[F]] = {{\[}}[[A]]]
; CHECK:       add [[A]] = [[A]], r{{[0-9]+}}
; CHECK:       stfd {{.*}} = [[F]]
; CHECK:       br.cloop
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = phi ptr [ %s, %entry ], [ %sp.next, %loop ]
  %dp = phi ptr [ %d, %entry ], [ %dp.next, %loop ]
  %v0 = load double, ptr %sp
  store double %v0, ptr %dp
  %sp1 = getelementptr inbounds i8, ptr %sp, i64 %ss
  %dp1 = getelementptr inbounds i8, ptr %dp, i64 %ds
  %v1 = load double, ptr %sp1
  store double %v1, ptr %dp1
  %sp.next = getelementptr inbounds i8, ptr %sp1, i64 %ss
  %dp.next = getelementptr inbounds i8, ptr %dp1, i64 %ds
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; Outside a loop the copy is left alone.
define void @copy_once(ptr %d, ptr %s) {
; CHECK-LABEL: copy_once:
; CHECK-NOT:   ld{{.*}}.sa
; CHECK:       br.ret
  %v0 = load double, ptr %s
  store double %v0, ptr %d
  %s1 = getelementptr inbounds i8, ptr %s, i64 800
  %d1 = getelementptr inbounds i8, ptr %d, i64 800
  %v1 = load double, ptr %s1
  store double %v1, ptr %d1
  ret void
}

; A store of a value not loaded in the block holds the load back no longer
; than the scheduler can move it.
define void @fill(ptr %d, ptr %s, i64 %ds, i64 %n, double %x) {
; CHECK-LABEL: fill:
; CHECK-NOT:   ld{{.*}}.sa
; CHECK:       br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = phi ptr [ %s, %entry ], [ %sp.next, %loop ]
  %dp = phi ptr [ %d, %entry ], [ %dp.next, %loop ]
  store double %x, ptr %dp
  %v = load double, ptr %sp
  %dp1 = getelementptr inbounds i8, ptr %dp, i64 %ds
  store double %v, ptr %dp1
  %sp.next = getelementptr inbounds i8, ptr %sp, i64 %ds
  %dp.next = getelementptr inbounds i8, ptr %dp1, i64 %ds
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; Integer loads mostly hit the first-level cache and are left alone.
define void @copy2_int(ptr %d, ptr %s, i64 %ds, i64 %ss, i64 %n) {
; CHECK-LABEL: copy2_int:
; CHECK-NOT:   ld{{.*}}.sa
; CHECK:       br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sp = phi ptr [ %s, %entry ], [ %sp.next, %loop ]
  %dp = phi ptr [ %d, %entry ], [ %dp.next, %loop ]
  %v0 = load i64, ptr %sp
  store i64 %v0, ptr %dp
  %sp1 = getelementptr inbounds i8, ptr %sp, i64 %ss
  %dp1 = getelementptr inbounds i8, ptr %dp, i64 %ds
  %v1 = load i64, ptr %sp1
  store i64 %v1, ptr %dp1
  %sp.next = getelementptr inbounds i8, ptr %sp1, i64 %ss
  %dp.next = getelementptr inbounds i8, ptr %dp1, i64 %ds
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
