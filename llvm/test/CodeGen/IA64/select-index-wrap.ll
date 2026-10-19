; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; An index that wraps, "if (k < 0) k += n; a[k]", selects between two bases
; instead of between two indices, so that the index is extended beside the
; compare rather than after the select.

define i8 @wrap_sext(ptr %block, i32 %x, i32 %n) {
; CHECK-LABEL: wrap_sext:
; CHECK:       sxt4 r14 = r34
; CHECK-NEXT:  adds r15 = -1, r33 ;;
; CHECK-NEXT:  add r14 = r32, r14
; CHECK-NEXT:  cmp4.lt p7, p6 = -1, r15
; CHECK-NEXT:  sxt4 r16 = r15 ;;
; CHECK-NEXT:  (p6) add r14 = r14, r16
; CHECK-NEXT:  (p7) add r14 = r32, r16 ;;
; CHECK-NEXT:  ld1 r8 = [r14]
  %k = add i32 %x, -1
  %c = icmp slt i32 %k, 0
  %s = select i1 %c, i32 %n, i32 0
  %k2 = add nsw i32 %s, %k
  %i = sext i32 %k2 to i64
  %a = getelementptr inbounds i8, ptr %block, i64 %i
  %v = load i8, ptr %a, align 1
  ret i8 %v
}

define i8 @wrap_zext(ptr %block, i32 %k, i32 %n, i32 %lim) {
; CHECK-LABEL: wrap_zext:
; CHECK:       zxt4 r14 = r34
; CHECK-NEXT:  cmp4.ltu p6, p7 = r33, r35
; CHECK-NEXT:  zxt4 r15 = r33 ;;
; CHECK-NEXT:  add r14 = r32, r14 ;;
; CHECK-NEXT:  (p7) add r14 = r14, r15
; CHECK-NEXT:  (p6) add r14 = r32, r15 ;;
; CHECK-NEXT:  ld1 r8 = [r14]
  %c = icmp ult i32 %k, %lim
  %s = select i1 %c, i32 0, i32 %n
  %k2 = add nuw i32 %k, %s
  %i = zext i32 %k2 to i64
  %a = getelementptr inbounds i8, ptr %block, i64 %i
  %v = load i8, ptr %a, align 1
  ret i8 %v
}

define i8 @wrap_select_of_adds(ptr %block, i32 %k, i32 %n, i1 %c) {
; CHECK-LABEL: wrap_select_of_adds:
; CHECK:       sxt4 r14 = r34
; CHECK-NEXT:  sxt4 r15 = r33 ;;
; CHECK-NEXT:  add r14 = r32, r14
; CHECK-NEXT:  tbit.z p7, p6 = r35, 0 ;;
; CHECK-NEXT:  (p7) add r14 = r14, r15
; CHECK-NEXT:  (p6) add r14 = r32, r15 ;;
; CHECK-NEXT:  ld1 r8 = [r14]
  %kn = add nsw i32 %k, %n
  %k2 = select i1 %c, i32 %k, i32 %kn
  %i = sext i32 %k2 to i64
  %a = getelementptr inbounds i8, ptr %block, i64 %i
  %v = load i8, ptr %a, align 1
  ret i8 %v
}

; The extension of a sum that may wrap is not the sum of the extensions.
define i8 @wrap_may_wrap(ptr %block, i32 %k, i32 %n) {
; CHECK-LABEL: wrap_may_wrap:
; CHECK:       add r14 = r33, r34
; CHECK-NEXT:  cmp4.lt p7, p6 = -1, r33 ;;
; CHECK-NEXT:  (p6) sxt4 r14 = r14
; CHECK-NEXT:  (p7) sxt4 r14 = r33 ;;
; CHECK-NEXT:  add r14 = r32, r14 ;;
; CHECK-NEXT:  ld1 r8 = [r14]
  %c = icmp slt i32 %k, 0
  %s = select i1 %c, i32 %n, i32 0
  %k2 = add i32 %s, %k
  %i = sext i32 %k2 to i64
  %a = getelementptr inbounds i8, ptr %block, i64 %i
  %v = load i8, ptr %a, align 1
  ret i8 %v
}
