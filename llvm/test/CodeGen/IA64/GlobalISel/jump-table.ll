; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; The table holds the distance from itself to each block, in eight bytes. Its
; address is in the linkage table, the entry is added to it, and the branch
; goes through b6. The linkage table entry's address is computed before the
; range check.
define i64 @f(i64 %a) {
; CHECK-LABEL: f:
; CHECK:       mov r8 = r0
; CHECK-NEXT:  addl r15 = @ltoff(.LJTI0_0), r1 ;;
; CHECK-NEXT:  cmp.ltu p6, p7 = 4, r14{{$}}
; CHECK-NEXT:  (p6) br.ret.dpnt.many b0 ;;
; CHECK:       .LBB0_1:
; CHECK-NEXT:  ld8 r15 = [r15] ;;
; CHECK-NEXT:  shladd r14 = r14, 3, r15 ;;
; CHECK-NEXT:  ld8 r14 = [r14] ;;
; CHECK-NEXT:  add r14 = r15, r14 ;;
; CHECK-NEXT:  mov b6 = r14
; CHECK-NEXT:  br.cond.sptk.few b6 ;;
; CHECK:       .LJTI0_0:
; CHECK-NEXT:  data8.ua .LBB0_2-.LJTI0_0
; CHECK-NEXT:  data8.ua .LBB0_6-.LJTI0_0
; CHECK-NEXT:  data8.ua .LBB0_4-.LJTI0_0
; CHECK-NEXT:  data8.ua .LBB0_5-.LJTI0_0
; CHECK-NEXT:  data8.ua .LBB0_3-.LJTI0_0
  switch i64 %a, label %d [ i64 0, label %b0
                            i64 1, label %b1
                            i64 2, label %b2
                            i64 3, label %b3
                            i64 4, label %b4 ]
b0:
  ret i64 10
b1:
  ret i64 21
b2:
  ret i64 35
b3:
  ret i64 47
b4:
  ret i64 59
d:
  ret i64 0
}
