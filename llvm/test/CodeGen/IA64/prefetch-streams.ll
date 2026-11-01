; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -mtune=madison -verify-machineinstrs \
; RUN:   < %s | FileCheck %s --check-prefixes=CHECK,MADISON
; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,DEFAULT

; On Madison a loop walking doubles through memory of unknown size prefetches
; them into L2. No other tuning does.

define double @unknown_size(ptr %a, i64 %n) {
; CHECK-LABEL: unknown_size:
; MADISON:       lfetch.nt1
; DEFAULT-NOT:   lfetch
; CHECK:         br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi double [ 0.0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %sum.next = fadd double %sum, %v
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret double %sum.next
}

; A walk known to fit in L2 stays there.

define double @fits_in_l2(ptr %a) {
; CHECK-LABEL: fits_in_l2:
; CHECK-NOT:     lfetch
; CHECK:         br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi double [ 0.0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr double, ptr %a, i64 %i
  %v = load double, ptr %p
  %sum.next = fadd double %sum, %v
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, 1000
  br i1 %done, label %exit, label %loop

exit:
  ret double %sum.next
}

; Words pay for an lfetch on too many elements of each line.

define i32 @words(ptr %a, i64 %n) {
; CHECK-LABEL: words:
; CHECK-NOT:     lfetch
; CHECK:         br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr i32, ptr %a, i64 %i
  %v = load i32, ptr %p
  %sum.next = add i32 %sum, %v
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %sum.next
}

; Two streams at most.

define double @three_streams(ptr %a, ptr %b, ptr %c, i64 %n) {
; CHECK-LABEL: three_streams:
; MADISON-COUNT-2: lfetch.nt1
; MADISON-NOT:     lfetch
; CHECK:           br.ret
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi double [ 0.0, %entry ], [ %sum.next, %loop ]
  %pa = getelementptr double, ptr %a, i64 %i
  %pb = getelementptr double, ptr %b, i64 %i
  %pc = getelementptr double, ptr %c, i64 %i
  %va = load double, ptr %pa
  %vb = load double, ptr %pb
  %vc = load double, ptr %pc
  %x = fadd double %va, %vb
  %y = fadd double %x, %vc
  %sum.next = fadd double %sum, %y
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret double %sum.next
}

; The pass simplifies the loops it wants a preheader for: the guard here leaves
; this one without.

define double @no_preheader(ptr %a, i64 %n, i1 %c) {
; CHECK-LABEL: no_preheader:
; MADISON:       lfetch.nt1
; DEFAULT-NOT:   lfetch
; CHECK:         br.ret
entry:
  br i1 %c, label %loop, label %exit

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi double [ 0.0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr inbounds double, ptr %a, i64 %i
  %v = load double, ptr %p
  %sum.next = fadd double %sum, %v
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi double [ 0.0, %entry ], [ %sum.next, %loop ]
  ret double %r
}

; Each pass of the outer loop walks the same bytes: the first left them cached.

define double @rewalked(ptr %a, i64 %n) {
; CHECK-LABEL: rewalked:
; CHECK-NOT:     lfetch
; CHECK:         br.ret
entry:
  br label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %latch ]
  %s.o = phi double [ 0.0, %entry ], [ %s.next, %latch ]
  br label %inner

inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %s = phi double [ %s.o, %outer ], [ %s.next, %inner ]
  %p = getelementptr double, ptr %a, i64 %j
  %v = load double, ptr %p
  %s.next = fadd double %s, %v
  %j.next = add i64 %j, 1
  %c = icmp ult i64 %j.next, %n
  br i1 %c, label %inner, label %latch

latch:
  %i.next = add i64 %i, 1
  %d = icmp ult i64 %i.next, %n
  br i1 %d, label %outer, label %exit

exit:
  ret double %s.next
}


; Rows through a pointer of their own: nothing says they are long.

define double @fp_row_pointers(ptr %a, i64 %n) {
; CHECK-LABEL: fp_row_pointers:
; CHECK-NOT:     lfetch
; CHECK:         br.ret
entry:
  br label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %latch ]
  %s.o = phi double [ 0.0, %entry ], [ %s.next, %latch ]
  %rp = getelementptr ptr, ptr %a, i64 %i
  %r = load ptr, ptr %rp
  br label %inner

inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %s = phi double [ %s.o, %outer ], [ %s.next, %inner ]
  %p = getelementptr double, ptr %r, i64 %j
  %v = load double, ptr %p
  %s.next = fadd double %s, %v
  %j.next = add i64 %j, 1
  %c = icmp ult i64 %j.next, %n
  br i1 %c, label %inner, label %latch

latch:
  %i.next = add i64 %i, 1
  %d = icmp ult i64 %i.next, %n
  br i1 %d, label %outer, label %exit

exit:
  ret double %s.next
}


; Integer rows through pointers keep their prefetch, for zstd.

define i64 @int_row_pointers(ptr %a, i64 %n) {
; CHECK-LABEL: int_row_pointers:
; MADISON:       lfetch.nt1
; CHECK:         br.ret
entry:
  br label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %latch ]
  %s.o = phi i64 [ 0, %entry ], [ %s.next, %latch ]
  %rp = getelementptr ptr, ptr %a, i64 %i
  %r = load ptr, ptr %rp
  br label %inner

inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %s = phi i64 [ %s.o, %outer ], [ %s.next, %inner ]
  %p = getelementptr i64, ptr %r, i64 %j
  %v = load i64, ptr %p
  %s.next = add i64 %s, %v
  %j.next = add i64 %j, 1
  %c = icmp ult i64 %j.next, %n
  br i1 %c, label %inner, label %latch

latch:
  %i.next = add i64 %i, 1
  %d = icmp ult i64 %i.next, %n
  br i1 %d, label %outer, label %exit

exit:
  ret i64 %s.next
}


; One object with a computed offset is still adjacent rows, for tramp3d.

define double @computed_rows(ptr %a, i64 %n) {
; CHECK-LABEL: computed_rows:
; MADISON:       lfetch.nt1
; CHECK:         br.ret
entry:
  br label %outer

outer:
  %i = phi i64 [ 0, %entry ], [ %i.next, %latch ]
  %s.o = phi double [ 0.0, %entry ], [ %s.next, %latch ]
  %m = mul i64 %i, %n
  %r = getelementptr double, ptr %a, i64 %m
  br label %inner

inner:
  %j = phi i64 [ 0, %outer ], [ %j.next, %inner ]
  %s = phi double [ %s.o, %outer ], [ %s.next, %inner ]
  %p = getelementptr double, ptr %r, i64 %j
  %v = load double, ptr %p
  %s.next = fadd double %s, %v
  %j.next = add i64 %j, 1
  %c = icmp ult i64 %j.next, %n
  br i1 %c, label %inner, label %latch

latch:
  %i.next = add i64 %i, 1
  %d = icmp ult i64 %i.next, %n
  br i1 %d, label %outer, label %exit

exit:
  ret double %s.next
}
