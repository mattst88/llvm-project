; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A doubleword with byte or halfword alignment is read from the aligned
; doublewords holding its first and last bytes. When it is aligned both are
; the same doubleword, and the count of 64 shifts the second one out.

define i64 @load8_align1(ptr %p) {
; CHECK-LABEL: load8_align1:
; CHECK-DAG:   adds [[END:r[0-9]+]] = 7, r32
; CHECK-DAG:   and [[LOADDR:r[0-9]+]] = -8, r32
; CHECK-DAG:   dep.z [[BITS:r[0-9]+]] = r32, 3, 3 ;;
; CHECK-DAG:   and [[HIADDR:r[0-9]+]] = -8, [[END]]
; CHECK-DAG:   ld8 [[LO:r[0-9]+]] = [[[LOADDR]]]
; CHECK-DAG:   sub [[REST:r[0-9]+]] = 64, [[BITS]] ;;
; CHECK-DAG:   ld8 [[HI:r[0-9]+]] = [[[HIADDR]]]
; CHECK-DAG:   shr.u [[LOPART:r[0-9]+]] = [[LO]], [[BITS]] ;;
; CHECK:       shl [[HIPART:r[0-9]+]] = [[HI]], [[REST]] ;;
; CHECK-NEXT:  or r8 = [[LOPART]], [[HIPART]]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i64, ptr %p, align 1
  ret i64 %v
}

define i64 @load8_align2(ptr %p) {
; CHECK-LABEL: load8_align2:
; CHECK-COUNT-2: ld8
; CHECK-NOT:   ld
; CHECK:       br.ret.sptk.many b0 ;;
  %v = load i64, ptr %p, align 2
  ret i64 %v
}

define double @load8_double_align1(ptr %p) {
; CHECK-LABEL: load8_double_align1:
; CHECK-COUNT-2: ld8
; CHECK-NOT:   ld
; CHECK:       setf.d f8 =
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load double, ptr %p, align 1
  ret double %v
}

; Word alignment is two words, and a volatile load reads only its own bytes.

define i64 @load8_align4(ptr %p) {
; CHECK-LABEL: load8_align4:
; CHECK:       ld4 [[LO:r[0-9]+]] = [r32], 4 ;;
; CHECK-NEXT:  ld4 [[HI:r[0-9]+]] = [r32] ;;
; CHECK-NEXT:  shl [[HI]] = [[HI]], 32 ;;
; CHECK-NEXT:  or r8 = [[HI]], [[LO]]
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

define i64 @load8_volatile_align1(ptr %p) {
; CHECK-LABEL: load8_volatile_align1:
; CHECK-COUNT-8: ld1
; CHECK-NOT:   ld8
; CHECK:       br.ret.sptk.many b0 ;;
  %v = load volatile i64, ptr %p, align 1
  ret i64 %v
}
