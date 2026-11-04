; RUN: llc -mtriple=ia64-unknown-linux-gnu -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s

; The source and destination of a memcpy do not overlap, so the stores an
; inlined copy expands to are marked as not aliasing its loads.

; MIR-LABEL: name: cp
; MIR: LD1_i {{.*}} :: (load (s8) from %ir.s, !alias.scope ![[S:[0-9]+]])
; MIR: ST1_i {{.*}} :: (store (s8) into %ir.d, !noalias ![[S]])
; MIR: LD1 {{.*}} :: (load (s8) from %ir.s + 1, !alias.scope ![[S]])
; MIR: ST1 {{.*}} :: (store (s8) into %ir.d + 1, !noalias ![[S]])

; That lets later loads issue alongside earlier stores.

; CHECK-LABEL: cp:
; CHECK:      ld1 [[A:r[0-9]+]] = [r33] ;;
; CHECK-NEXT: ld1
; CHECK-NEXT: ld1
; CHECK-NEXT: ld1 {{.*}} ;;
; CHECK-NEXT: st1 [r32] = [[A]]
; CHECK-NEXT: st1
; CHECK-NEXT: st1
; CHECK-NEXT: st1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define void @cp(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 4, i1 false)
  ret void
}

; A volatile copy keeps its accesses in order.

; MIR-LABEL: name: vol
; MIR: LD1_i {{.*}} :: (volatile load (s8) from %ir.s)
; MIR: ST1_i {{.*}} :: (volatile store (s8) into %ir.d)

; CHECK-LABEL: vol:
; CHECK:      ld1 [[A:r[0-9]+]] = [r33]
; CHECK:      st1 [r32] = [[A]]
; CHECK-NEXT: ld1
; CHECK:      st1
; CHECK-NEXT: ld1 {{.*}} ;;
; CHECK-NEXT: st1
; CHECK-NEXT: ld1 {{.*}} ;;
; CHECK-NEXT: st1
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define void @vol(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 4, i1 true)
  ret void
}

; A memmove may overlap.

; MIR-LABEL: name: mv
; MIR: LD1_i {{.*}} :: (load (s8) from %ir.s)
; MIR: ST1_i {{.*}} :: (store (s8) into %ir.d)
define void @mv(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 4, i1 false)
  ret void
}
