; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A prefetch names the levels of the cache that the line need not be brought
; into, and there are more of them the less the program expects to use the
; line again.

declare void @llvm.prefetch.p0(ptr, i32, i32, i32)

define void @read(ptr %p) {
; CHECK-LABEL: read:
; CHECK:         lfetch [r32]
; CHECK-NEXT:    lfetch.nt1 [r32]
; CHECK-NEXT:    lfetch.nt2 [r32]
; CHECK-NEXT:    lfetch.nta [r32]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 3, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 2, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 1, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 0, i32 1)
  ret void
}

; A line that is to be written is asked for with nobody else holding it.

define void @write(ptr %p) {
; CHECK-LABEL: write:
; CHECK:         lfetch.excl [r32]
; CHECK-NEXT:    lfetch.excl.nt1 [r32]
; CHECK-NEXT:    lfetch.excl.nt2 [r32]
; CHECK-NEXT:    lfetch.excl.nta [r32]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 3, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 2, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 1, i32 1)
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 0, i32 1)
  ret void
}

; There is one instruction for code and for data.

define void @code(ptr %p) {
; CHECK-LABEL: code:
; CHECK:         lfetch [r32]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 3, i32 0)
  ret void
}

define void @element(ptr %p, i64 %i) {
; CHECK-LABEL: element:
; CHECK:         shladd r14 = r33, 3, r32 ;;
; CHECK-NEXT:    lfetch [r14]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %q = getelementptr i64, ptr %p, i64 %i
  call void @llvm.prefetch.p0(ptr %q, i32 0, i32 3, i32 1)
  ret void
}

; A prefetch reads nothing into a register and writes no memory, so the
; arithmetic moves across it, and it shares a group with the first load.

define i64 @reorder(ptr %p, ptr %q) {
; CHECK-LABEL: reorder:
; CHECK:         ld8 r14 = [r33], 8
; CHECK-NEXT:    lfetch [r32] ;;
; CHECK-NEXT:    ld8 r15 = [r33] ;;
; CHECK-NEXT:    add r8 = r14, r15, 1
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %x = load i64, ptr %q
  %y = add i64 %x, 1
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 3, i32 1)
  %r = getelementptr i64, ptr %q, i64 1
  %z = load i64, ptr %r
  %s = add i64 %y, %z
  ret i64 %s
}
