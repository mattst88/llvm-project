; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 < %s | FileCheck %s

; A scalar is taken apart into the elements of a vector by shifts where the
; elements are not whole bytes, and three bytes are put together by shifts.
; Widening the parts of the first made an unmerge that the legalizer folded
; back into the one it had started from, without end.

; CHECK-LABEL: bits:
; CHECK:      mov r8 = r32
; CHECK-NEXT: shr.u r9 = r32, 1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define <2 x i1> @bits(i2 %a) {
  %x = bitcast i2 %a to <2 x i1>
  ret <2 x i1> %x
}

; CHECK-LABEL: sevens:
; CHECK:      mov r14 = 16383
; CHECK-NEXT: mov r8 = r32 ;;
; CHECK-NEXT: and r14 = r32, r14 ;;
; CHECK-NEXT: shr.u r9 = r14, 7
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define <2 x i7> @sevens(i14 %a) {
  %x = bitcast i14 %a to <2 x i7>
  ret <2 x i7> %x
}

; CHECK-LABEL: bytes:
; CHECK:      zxt1 r14 = r33
; CHECK-NEXT: zxt1 r15 = r32 ;;
; CHECK-NEXT: shl r14 = r14, 8
; CHECK-NEXT: zxt1 r16 = r34 ;;
; CHECK-NEXT: or r14 = r15, r14
; CHECK-NEXT: shl r17 = r16, 16 ;;
; CHECK-NEXT: or r8 = r14, r17
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i24 @bytes(<3 x i8> %a) {
  %x = bitcast <3 x i8> %a to i24
  ret i24 %x
}
