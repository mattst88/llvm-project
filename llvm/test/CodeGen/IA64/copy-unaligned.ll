; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -verify-machineinstrs < %s | FileCheck %s

; A copy of 8n bytes to a destination with byte alignment stores the first
; byte, halfword and word at the first addresses aligned for them, every
; aligned doubleword, and the last word, halfword and byte. The stores may
; overlap, each holding the bytes that belong at its own address. The source
; is read as the aligned doublewords holding it, before any store.

; CHECK-LABEL: copy16:
; CHECK-COUNT-3: ld8
; CHECK-NOT:     ld
; CHECK-DAG:     st1 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st1 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st2 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st2 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st8 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st8 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy16(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; The address of the last doubleword is the aligned address before the end,
; and its bits start 8 - (d & 7) bytes into the first source doubleword.
; CHECK-LABEL: move16:
; CHECK-DAG:     and [[DOWN:r[0-9]+]] = -8, r32
; CHECK-DAG:     adds [[LAST:r[0-9]+]] = 8, [[DOWN]]
; CHECK-DAG:     dep.z [[T:r[0-9]+]] = r32, 3, 3
; CHECK-DAG:     shl {{r[0-9]+}} = {{r[0-9]+}}, [[T]]
; CHECK-DAG:     xor [[K:r[0-9]+]] = 56, [[T]]
; CHECK-DAG:     shr.u {{r[0-9]+}} = {{r[0-9]+}}, [[K]]
; CHECK-DAG:     st8 [[[LAST]]] =
; CHECK:         br.ret
define void @move16(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; Eight bytes have no aligned doubleword to store when unaligned.
; CHECK-LABEL: copy8:
; CHECK-COUNT-2: ld8
; CHECK-NOT:     ld
; CHECK-DAG:     st1 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st1 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st2 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st2 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy8(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 8, i1 false)
  ret void
}

; An aligned source is read directly.
; CHECK-LABEL: copy16_src8:
; CHECK-COUNT-2: ld8 {{r[0-9]+}} = [
; CHECK-NOT:     ld
; CHECK-NOT:     dep.z {{r[0-9]+}} = r33
; CHECK:         br.ret
define void @copy16_src8(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 8 %s, i64 16, i1 false)
  ret void
}

; 64 bytes would take more stores than the inline limit a byte at a time.
; CHECK-LABEL: copy64:
; CHECK-NOT:     br.call
; CHECK-COUNT-9: ld8
; CHECK-NOT:     br.call
; CHECK:         br.ret
define void @copy64(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 64, i1 false)
  ret void
}

; Pieces smaller than the destination's alignment are left out.
; CHECK-LABEL: copy16_dst4:
; CHECK-COUNT-3: ld8
; CHECK-NOT:     ld
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st4 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st8 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-DAG:     st8 [{{r[0-9]+}}] = {{r[0-9]+}}
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy16_dst4(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; CHECK-LABEL: copy16_dst8:
; CHECK-COUNT-3: ld8
; CHECK-NOT:     ld
; CHECK-COUNT-2: st8
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy16_dst8(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 1 %s, i64 16, i1 false)
  ret void
}

; Fewer stores go the generic way: aligned words, sizes that are not whole
; doublewords, and volatile copies.
; CHECK-LABEL: copy16_both4:
; CHECK-COUNT-4: st4
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy16_both4(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %d, ptr align 4 %s, i64 16, i1 false)
  ret void
}

; CHECK-LABEL: copy12:
; CHECK-COUNT-12: st1
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy12(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 12, i1 false)
  ret void
}

; CHECK-LABEL: copy16_volatile:
; CHECK-COUNT-16: st1
; CHECK-NOT:     st
; CHECK:         br.ret
define void @copy16_volatile(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 16, i1 true)
  ret void
}
