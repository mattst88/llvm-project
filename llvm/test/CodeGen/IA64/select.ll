; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The result starts as the false value, and the true one replaces it under
; the predicate of the compare.

define i64 @select_eq(i64 %a, i64 %b, i64 %x, i64 %y) {
; CHECK-LABEL: select_eq:
; CHECK:       mov r8 = r35
; CHECK-NEXT:  cmp.eq p6, p7 = r32, r33 ;;
; CHECK-NEXT:  (p6) mov r8 = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %c = icmp eq i64 %a, %b
  %r = select i1 %c, i64 %x, i64 %y
  ret i64 %r
}

define i64 @select_slt_constant(i64 %a, i64 %x, i64 %y) {
; CHECK-LABEL: select_slt_constant:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  cmp.lt p7, p6 = 9, r32 ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %c = icmp slt i64 %a, 10
  %r = select i1 %c, i64 %x, i64 %y
  ret i64 %r
}

define i64 @max(i64 %a, i64 %b) {
; CHECK-LABEL: max:
; CHECK:       mov r8 = r33 ;;
; CHECK-NEXT:  cmp.lt p6, p7 = r8, r32 ;;
; CHECK-NEXT:  (p6) mov r8 = r32
  %c = icmp sgt i64 %a, %b
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

; A condition that is a value is compared with zero.

define i64 @select_value(i1 %c, i64 %x, i64 %y) {
; CHECK-LABEL: select_value:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  and [[C:r[0-9]+]] = 1, r32 ;;
; CHECK-NEXT:  cmp.eq p7, p6 = 0, [[C]] ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %r = select i1 %c, i64 %x, i64 %y
  ret i64 %r
}

define i32 @select_narrow(i32 %a, i32 %x, i32 %y) {
; CHECK-LABEL: select_narrow:
; CHECK:       mov r8 = r34
; CHECK-NEXT:  sxt4 [[A:r[0-9]+]] = r32 ;;
; CHECK-NEXT:  cmp.lt p6, p7 = 0, [[A]] ;;
; CHECK-NEXT:  (p6) mov r8 = r33
  %c = icmp sgt i32 %a, 0
  %r = select i1 %c, i32 %x, i32 %y
  ret i32 %r
}

define i64 @select_constants(i64 %a) {
; CHECK-LABEL: select_constants:
; CHECK:       mov [[T:r[0-9]+]] = 10
; CHECK-NEXT:  mov r8 = 20
; CHECK-NEXT:  cmp.eq p6, p7 = 0, r32 ;;
; CHECK-NEXT:  (p6) mov r8 = [[T]]
  %c = icmp eq i64 %a, 0
  %r = select i1 %c, i64 10, i64 20
  ret i64 %r
}
