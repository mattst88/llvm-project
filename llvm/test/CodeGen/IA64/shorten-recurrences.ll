; RUN: llc -mtriple=ia64 -O2 -mcpu=madison -stop-after=ia64-shorten-recurrences \
; RUN:   < %s | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=ia64 -O2 -stop-after=ia64-shorten-recurrences < %s \
; RUN:   | FileCheck %s --check-prefix=GENERIC
; RUN: llc -mtriple=ia64 -O2 -mcpu=madison < %s | FileCheck %s --check-prefix=ASM

; CPython's k_mul adds a digit and a product to a carry and shifts it down:
;   carry += *pz + *pa++ * f;
; Reassociate adds the carry to the digit first, which puts two adds and
; the shift on the carry's path around the loop. Adding the digit to the
; product first leaves one add, and the pipelined loop starts an iteration
; every two cycles instead of three.

; IR-LABEL: @carry(
; IR: %[[OFF:.*]] = add i64 %conv, %mul
; IR-NEXT: %add2 = add i64 %carry, %[[OFF]]
; IR-NOT: add nuw nsw i64 %carry

; The pass runs only for a Madison: an Itanium 9340 was slower with it.
; GENERIC-LABEL: @carry(
; GENERIC: %add = add nuw nsw i64 %carry, %conv

; The machine combiner must not move the carry back under the other adds,
; and the kernel takes two instruction groups.
; ASM-LABEL: carry:
; ASM: .LBB0_[[L:[0-9]+]]:
; ASM-NOT: ;;
; ASM: ;;
; ASM-NOT: ;;
; ASM: br.ctop.sptk.few .LBB0_[[L]] ;;
define void @carry(ptr noalias %pz, ptr noalias %pa, i64 %n, i64 %f) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %carry = phi i64 [ 0, %entry ], [ %shr, %loop ]
  %pz.i = getelementptr inbounds i32, ptr %pz, i64 %i
  %pa.i = getelementptr inbounds i32, ptr %pa, i64 %i
  %z = load i32, ptr %pz.i, align 4
  %conv = zext i32 %z to i64
  %a = load i32, ptr %pa.i, align 4
  %conv1 = zext i32 %a to i64
  %mul = mul i64 %f, %conv1
  %add = add nuw nsw i64 %carry, %conv
  %add2 = add i64 %add, %mul
  %t = trunc i64 %add2 to i32
  %digit = and i32 %t, 1073741823
  store i32 %digit, ptr %pz.i, align 4
  %shr = lshr i64 %add2, 30
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

; Every add of a longer sum moves off the path, one at a time.
; IR-LABEL: @three(
; IR: %[[A:.*]] = add i64 %x, %y
; IR-NEXT: %[[B:.*]] = add i64 %[[A]], %z
; IR-NEXT: %s3 = add i64 %h, %[[B]]
define i64 @three(ptr %p, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %h = phi i64 [ 0, %entry ], [ %s3, %loop ]
  %px = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %px, align 8
  %y = shl i64 %x, 7
  %z = lshr i64 %x, 3
  %s1 = add i64 %h, %x
  %s2 = add i64 %s1, %y
  %s3 = add i64 %s2, %z
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i64 %s3
}

; An operand computed from the phi stays where it is: the new add would
; wait for it, and the path would be no shorter.
; IR-LABEL: @dependent(
; IR: %s1 = add i64 %h, %x
; IR-NEXT: %s2 = add i64 %s1, %y
define i64 @dependent(ptr %p, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %h = phi i64 [ 0, %entry ], [ %s2, %loop ]
  %px = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %px, align 8
  %y = shl i64 %h, 5
  %s1 = add i64 %h, %x
  %s2 = add i64 %s1, %y
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i64 %s2
}

; A sum that does not reach the phi's next value is not on a recurrence.
; IR-LABEL: @not_carried(
; IR: %s1 = add i64 %h, %x
; IR-NEXT: %s2 = add i64 %s1, %y
define i64 @not_carried(ptr %p, ptr %q, i64 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %h = phi i64 [ 0, %entry ], [ %x, %loop ]
  %px = getelementptr inbounds i64, ptr %p, i64 %i
  %x = load i64, ptr %px, align 8
  %y = shl i64 %x, 5
  %s1 = add i64 %h, %x
  %s2 = add i64 %s1, %y
  %qx = getelementptr inbounds i64, ptr %q, i64 %i
  store i64 %s2, ptr %qx, align 8
  %i.next = add nuw i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i64 %h
}
