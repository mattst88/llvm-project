; RUN: llc -mtriple=ia64-unknown-linux-gnu -mattr=+constant-gp \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A program that has one global pointer gets it back from every function it
; calls by name, and has no need to keep a copy to put back.

declare i64 @ext(i64)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define i64 @by_name(i64 %a) {
; CHECK-LABEL: by_name:
; CHECK:       alloc r33 = ar.pfs, 1, 2, 1, 0
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov r35 = r32 ;;
; CHECK-NEXT:  br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  adds r8 = 1, r8
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov ar.pfs = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call i64 @ext(i64 %a)
  %s = add i64 %r, 1
  ret i64 %s
}

; A function of the library is called by name too.
define void @library(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: library:
; CHECK-NOT:   {{ r1($| )}}
; CHECK:       br.call.sptk.many b0 = memcpy ;;
; CHECK-NOT:   {{ r1($| )}}
; CHECK:       br.ret.sptk.many b0 ;;
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  ret void
}

; A descriptor still says what the global pointer of its function is, and a
; call through one takes it from there and puts this function's back.
define i64 @indirect(ptr %f, i64 %a) {
; CHECK-LABEL: indirect:
; CHECK:       ld8 r14 = [r32]
; CHECK-NEXT:  adds r15 = 8, r32
; CHECK-NEXT:  mov r36 = r33
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  ld8 r15 = [r15]
; CHECK-NEXT:  mov b6 = r14 ;;
; CHECK-NEXT:  mov r1 = r15
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
; CHECK-NEXT:  mov r1 = r32
  %r = call i64 %f(i64 %a)
  ret i64 %r
}
