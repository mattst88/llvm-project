; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; No vector is legal, so a compare of two is a compare of each pair of
; elements, and it has to say that its result is a vector for that to be done.

define <2 x i64> @eq(<2 x i64> %a, <2 x i64> %b) {
; CHECK-LABEL: eq:
; CHECK-DAG:   cmp.eq p6, p7 = r32, r34
; CHECK-DAG:   cmp.eq p6, p7 = r33, r35
; CHECK:       br.ret.sptk.many b0
  %c = icmp eq <2 x i64> %a, %b
  %r = sext <2 x i1> %c to <2 x i64>
  ret <2 x i64> %r
}
