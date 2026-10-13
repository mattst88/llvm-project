; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; The elements of a vector that are not whole bytes are packed in memory
; with nothing between them, the first of them lowest, and the vector is
; stored and loaded as the one integer they make.

define void @store(<4 x i1> %v, ptr %p) {
; CHECK-LABEL: store:
; CHECK-DAG:   and [[A:r[0-9]+]] = 1, r32
; CHECK-DAG:   and [[B:r[0-9]+]] = 1, r33
; CHECK-DAG:   and [[C:r[0-9]+]] = 1, r34
; CHECK-DAG:   and [[D:r[0-9]+]] = 1, r35
; CHECK-DAG:   shl [[B1:r[0-9]+]] = [[B]], 1
; CHECK-DAG:   shl [[C2:r[0-9]+]] = [[C]], 2
; CHECK-DAG:   shl [[D3:r[0-9]+]] = [[D]], 3
; CHECK:       st1 [r36] = r{{[0-9]+}}
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  store <4 x i1> %v, ptr %p
  ret void
}

define <4 x i1> @load(ptr %p) {
; CHECK-LABEL: load:
; CHECK:       ld1 r8 = [r32] ;;
; CHECK-DAG:   shr.u r9 = r8, 1
; CHECK-DAG:   shr.u r10 = r8, 2
; CHECK-DAG:   shr.u r11 = r8, 3
; CHECK:       br.ret.sptk.many b0 ;;
  %v = load <4 x i1>, ptr %p
  ret <4 x i1> %v
}

define void @odd(ptr %p, ptr %q) {
; CHECK-LABEL: odd:
; CHECK:       ld2 r{{[0-9]+}} = [r32]
; CHECK:       st2 [r33] = r{{[0-9]+}}
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load <3 x i5>, ptr %p
  %w = add <3 x i5> %v, <i5 1, i5 2, i5 3>
  store <3 x i5> %w, ptr %q
  ret void
}
