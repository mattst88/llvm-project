; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; A test of a single bit is a tbit, which needs neither the mask nor an and.
; tbit.z sets its first predicate if the bit is clear; tbit.nz would only swap
; the predicates, so it is not used.

declare void @f()

define void @branch_clear(i64 %x) {
; CHECK-LABEL: branch_clear:
; CHECK-NOT:   and
; CHECK:       tbit.z p7, p6 = r32, 3
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_2 ;;
; CHECK-NEXT:  // %bb.1:
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  br.call.sptk.many b0 = f
  %a = and i64 %x, 8
  %c = icmp eq i64 %a, 0
  br i1 %c, label %t, label %e
t:
  call void @f()
  ret void
e:
  ret void
}

define void @branch_set(i64 %x) {
; CHECK-LABEL: branch_set:
; CHECK:       tbit.z p6, p7 = r32, 3
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB1_2 ;;
; CHECK-NEXT:  // %bb.1:
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  br.call.sptk.many b0 = f
  %a = and i64 %x, 8
  %c = icmp ne i64 %a, 0
  br i1 %c, label %t, label %e
t:
  call void @f()
  ret void
e:
  ret void
}

; A bit too high for an immediate mask needs no movl.

define i64 @select_high(i64 %x, i64 %a, i64 %b) {
; CHECK-LABEL: select_high:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  tbit.z p7, p6 = r32, 40 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %m = and i64 %x, 1099511627776
  %c = icmp ne i64 %m, 0
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i64 @select_sign(i64 %x, i64 %a, i64 %b) {
; CHECK-LABEL: select_sign:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  tbit.z p6, p7 = r32, 63 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %m = and i64 %x, -9223372036854775808
  %c = icmp eq i64 %m, 0
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i32 @select_narrow(i32 %x, i32 %a, i32 %b) {
; CHECK-LABEL: select_narrow:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  tbit.z p7, p6 = r32, 10 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %m = and i32 %x, 1024
  %c = icmp ne i32 %m, 0
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; In a chain, a bit test is a parallel tbit. An AND chain clears p6 when the
; bit is not as tested.

define i64 @select_and(i64 %x, i64 %y, i64 %p, i64 %q) {
; CHECK-LABEL: select_and:
; CHECK:       cmp.lt p6, p7 = 5, r33
; CHECK-NEXT:  mov r8 = r35 ;;
; CHECK-NEXT:  tbit.z.or.andcm p7, p6 = r32, 3 ;;
; CHECK-NEXT:  (p6) mov r8 = r34
  %a = and i64 %x, 8
  %c = icmp ne i64 %a, 0
  %d = icmp sgt i64 %y, 5
  %o = and i1 %d, %c
  %r = select i1 %o, i64 %p, i64 %q
  ret i64 %r
}

define i64 @select_and_clear(i64 %x, i64 %y, i64 %p, i64 %q) {
; CHECK-LABEL: select_and_clear:
; CHECK:       cmp.lt p6, p7 = 5, r33
; CHECK-NEXT:  mov r8 = r35 ;;
; CHECK-NEXT:  tbit.nz.or.andcm p7, p6 = r32, 3 ;;
; CHECK-NEXT:  (p6) mov r8 = r34
  %a = and i64 %x, 8
  %c = icmp eq i64 %a, 0
  %d = icmp sgt i64 %y, 5
  %o = and i1 %d, %c
  %r = select i1 %o, i64 %p, i64 %q
  ret i64 %r
}

define void @branch_chain(i64 %x, i64 %y) {
; CHECK-LABEL: branch_chain:
; CHECK:       mov r34 = r1
; CHECK:       tbit.z p7, p6 = r32, 3 ;;
; CHECK-NEXT:  tbit.z.or.andcm p6, p7 = r33, 4
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB7_3{{$}}
  %a = and i64 %x, 8
  %c = icmp ne i64 %a, 0
  br i1 %c, label %t, label %n, !prof !0
n:
  %b = and i64 %y, 16
  %d = icmp eq i64 %b, 0
  br i1 %d, label %t, label %e, !prof !0
t:
  call void @f()
  ret void
e:
  ret void
}

; The sign of a byte or halfword is its top bit, which a tbit tests without
; extending the value. A word's sign is still a cmp4, which unlike a tbit can
; issue on an M-unit.

define i64 @select_byte_sign(ptr %p, i64 %a, i64 %b) {
; CHECK-LABEL: select_byte_sign:
; CHECK:       ld1 [[V:r[0-9]+]] = [r32]
; CHECK-NEXT:  mov r8 = r34 ;;
; CHECK-NEXT:  tbit.z p6, p7 = [[V]], 7 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %v = load i8, ptr %p
  %c = icmp sgt i8 %v, -1
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i64 @select_half_sign(i64 %x, i64 %a, i64 %b) {
; CHECK-LABEL: select_half_sign:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  tbit.z p7, p6 = r32, 15 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %t = trunc i64 %x to i16
  %c = icmp slt i16 %t, 0
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i64 @select_word_sign(i64 %x, i64 %a, i64 %b) {
; CHECK-LABEL: select_word_sign:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  cmp4.lt p7, p6 = -1, r32 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %t = trunc i64 %x to i32
  %c = icmp slt i32 %t, 0
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

; OBJ-LABEL: <branch_clear>:
; OBJ:       tbit.z p7, p6 = r32, 3
; OBJ-LABEL: <select_and_clear>:
; OBJ:       tbit.nz.or.andcm p7, p6 = r32, 3

!0 = !{!"branch_weights", i32 1, i32 100}
