; RUN: llc -mtriple=ia64-unknown-linux-gnu -ia64-min-jump-table-entries=4 \
; RUN:   -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; Every call writes r1, but the call itself restores this function's GP, so
; an address formed from r1 is the same before and after it.

@g = external global i64
declare void @f()
declare i32 @f32()

; The address of @g is loaded once, and reused after a call in another block.
define i64 @twice(i1 %c) {
; CHECK-LABEL: twice:
; CHECK:       addl [[E:r[0-9]+]] = @ltoffx(g), r1
; CHECK:       ld8.mov [[A:r[0-9]+]] = [[[E]]], g
; CHECK:       ld8 {{r[0-9]+}} = [[[A]]]
; CHECK:       br.call.sptk.many b0 = f
; CHECK-NOT:   @ltoffx(g)
; CHECK:       ld8 {{r[0-9]+}} = [[[A]]]
; CHECK:       .endp twice
entry:
  %a = load i64, ptr @g
  br i1 %c, label %call, label %done
call:
  call void @f()
  br label %done
done:
  %b = load i64, ptr @g
  %r = add i64 %a, %b
  ret i64 %r
}

; The jump table's address is loaded before the loop, which makes a call.
; CHECK-LABEL: dispatch:
; CHECK:       addl [[E:r[0-9]+]] = @ltoffx(.LJTI1_0), r1
; CHECK:       ld8.mov [[T:r[0-9]+]] = [[[E]]], .LJTI1_0
; CHECK:       Loop Header
; CHECK-NOT:   @ltoffx
; CHECK:       shladd {{r[0-9]+}} = {{r[0-9]+}}, 3, [[T]]
; CHECK:       .endp dispatch
define i32 @dispatch(ptr %p, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %next ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %next ]
  %a = getelementptr i32, ptr %p, i64 %i
  %v = load i32, ptr %a
  switch i32 %v, label %next [
    i32 0, label %call
    i32 1, label %mul
    i32 2, label %xor
    i32 3, label %sub
    i32 4, label %add
  ]
call:
  %c = call i32 @f32()
  %s0 = add i32 %s, %c
  br label %next
mul:
  %s1 = mul i32 %s, 3
  br label %next
xor:
  %s2 = xor i32 %s, 7
  br label %next
sub:
  %s3 = sub i32 %s, 11
  br label %next
add:
  %s4 = add i32 %s, 9
  br label %next
next:
  %s.next = phi i32 [ %s, %loop ], [ %s0, %call ], [ %s1, %mul ], [ %s2, %xor ], [ %s3, %sub ], [ %s4, %add ]
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %s.next
}
