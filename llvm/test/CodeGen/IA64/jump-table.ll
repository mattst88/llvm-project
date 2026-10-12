; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A switch reads the distance from the table to the block out of the table,
; whose address is in the linkage table, and branches through b6.
define i64 @f(i64 %a) {
; CHECK-LABEL: f:
; CHECK:       cmp.ltu p6, p7 = 4, r32 ;;
; CHECK-NEXT:  (p6) mov r8 = r0
; CHECK-NEXT:  (p6) br.ret.dpnt.many b0 ;;
; CHECK:       addl r14 = @ltoffx(.LJTI0_0), r1 ;;
; CHECK-NEXT:  ld8.mov r14 = [r14], .LJTI0_0 ;;
; CHECK-NEXT:  shladd r15 = r32, 2, r14 ;;
; CHECK-NEXT:  ld4 r15 = [r15] ;;
; CHECK-NEXT:  sxt4 r15 = r15 ;;
; CHECK-NEXT:  add r14 = r14, r15 ;;
; CHECK-NEXT:  mov b6 = r14
; CHECK-NEXT:  br.cond.sptk.few b6 ;;
; CHECK:       .section .rodata
; CHECK-NEXT:  .p2align 2
; CHECK:       .LJTI0_0:
; CHECK-NEXT:  data4.ua .LBB0_2-.LJTI0_0
; CHECK-NEXT:  data4.ua .LBB0_6-.LJTI0_0
; CHECK-NEXT:  data4.ua .LBB0_4-.LJTI0_0
; CHECK-NEXT:  data4.ua .LBB0_5-.LJTI0_0
; CHECK-NEXT:  data4.ua .LBB0_3-.LJTI0_0
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

@tab = internal constant [2 x ptr] [ptr blockaddress(@g, %x), ptr blockaddress(@g, %y)]

; A branch to a computed address goes through b6 as well.
define i64 @g(i64 %i) {
; CHECK-LABEL: g:
; CHECK:       shladd r14 = r32, 3, r14 ;;
; CHECK-NEXT:  ld8 r14 = [r14] ;;
; CHECK-NEXT:  mov b6 = r14
; CHECK-NEXT:  br.cond.sptk.few b6 ;;
; CHECK-NEXT:  .Ltmp0:
; CHECK:       .Ltmp1:
  %p = getelementptr [2 x ptr], ptr @tab, i64 0, i64 %i
  %t = load ptr, ptr %p
  indirectbr ptr %t, [label %x, label %y]
x:
  ret i64 1
y:
  ret i64 2
}

; The address of a block comes out of the linkage table.
define ptr @h() {
; CHECK-LABEL: h:
; CHECK:       addl r14 = @ltoffx(.Ltmp1), r1 ;;
; CHECK-NEXT:  ld8.mov r8 = [r14], .Ltmp1
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret ptr blockaddress(@g, %y)
}

; CHECK:       tab:
; CHECK-NEXT:  data8.ua .Ltmp0
; CHECK-NEXT:  data8.ua .Ltmp1
