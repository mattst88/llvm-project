; RUN: llc -mtriple=ia64 -O2 -stop-after=ia64-setf-phis < %s | FileCheck %s

; zstd's hash loops hash the next position at the bottom of an iteration
; and the same value with another prime at the top of the next one. The
; value is already in an FR on both edges into the loop.

; CHECK-LABEL: name: two_primes
; CHECK:       bb.0.entry:
; CHECK:         [[V0:%[0-9]+]]:gr, {{.*}} = LD8_i
; CHECK:         [[F0:%[0-9]+]]:fr = SETF_SIG [[V0]]
; CHECK:       bb.1.loop:
; CHECK:         {{%[0-9]+}}:fr = PHI [[F0]], %bb.0, [[F1:%[0-9]+]], %bb.2
; CHECK:         [[V:%[0-9]+]]:gr = PHI [[V0]], %bb.0
; CHECK-NOT:     SETF_SIG [[V]],
; CHECK:       bb.2.latch:
; CHECK:         [[V1:%[0-9]+]]:gr, {{.*}} = LD8_i
; CHECK:         [[F1]]:fr = SETF_SIG [[V1]]
define void @two_primes(ptr %p, ptr %out, i64 %n, i64 %a, i64 %b) {
entry:
  %v0 = load i64, ptr %p
  %h0 = mul i64 %v0, %a
  store i64 %h0, ptr %out
  br label %loop

loop:
  %i = phi i64 [ 1, %entry ], [ %i.next, %latch ]
  %v = phi i64 [ %v0, %entry ], [ %v1, %latch ]
  %m = mul i64 %v, %b
  %slot = getelementptr i64, ptr %out, i64 %i
  store i64 %m, ptr %slot
  %done = icmp eq i64 %m, 0
  br i1 %done, label %exit, label %latch

latch:
  %q = getelementptr i64, ptr %p, i64 %i
  %v1 = load i64, ptr %q
  %h1 = mul i64 %v1, %a
  store i64 %h1, ptr %out
  %i.next = add i64 %i, 1
  %more = icmp ult i64 %i.next, %n
  br i1 %more, label %loop, label %exit

exit:
  ret void
}

; Nothing moves %v to an FR on the back edge, so the setf.sig stays.

; CHECK-LABEL: name: no_setf_on_back_edge
; CHECK:         [[V0:%[0-9]+]]:gr, {{.*}} = LD8_i
; CHECK:       bb.1.loop:
; CHECK-NOT:     :fr = PHI
; CHECK:         [[V:%[0-9]+]]:gr = PHI [[V0]], %bb.0
; CHECK:         SETF_SIG [[V]],
define void @no_setf_on_back_edge(ptr %p, ptr %out, i64 %n, i64 %a, i64 %b) {
entry:
  %v0 = load i64, ptr %p
  %h0 = mul i64 %v0, %a
  store i64 %h0, ptr %out
  br label %loop

loop:
  %i = phi i64 [ 1, %entry ], [ %i.next, %latch ]
  %v = phi i64 [ %v0, %entry ], [ %v1, %latch ]
  %m = mul i64 %v, %b
  %slot = getelementptr i64, ptr %out, i64 %i
  store i64 %m, ptr %slot
  %done = icmp eq i64 %m, 0
  br i1 %done, label %exit, label %latch

latch:
  %q = getelementptr i64, ptr %p, i64 %i
  %v1 = load i64, ptr %q
  %i.next = add i64 %i, 1
  %more = icmp ult i64 %i.next, %n
  br i1 %more, label %loop, label %exit

exit:
  ret void
}
