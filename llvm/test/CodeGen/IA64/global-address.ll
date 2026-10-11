; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The address of a symbol is in the linkage table, at an offset from the
; global pointer that the linker fills in.

@x = global i64 1
@a = external global [10 x i64]
declare void @f()

define i64 @read() {
; CHECK-LABEL: read:
; CHECK:         addl r14 = @ltoff(x), r1 ;;
; CHECK-NEXT:    ld8 r14 = [r14] ;;
; CHECK-NEXT:    ld8 r8 = [r14]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %v = load i64, ptr @x
  ret i64 %v
}

define void @write(i64 %v) {
; CHECK-LABEL: write:
; CHECK:         addl r14 = @ltoff(x), r1 ;;
; CHECK-NEXT:    ld8 r14 = [r14] ;;
; CHECK-NEXT:    st8 [r14] = r32
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  store i64 %v, ptr @x
  ret void
}

; An offset is added to what the entry holds, so that the symbol has the one
; entry.

define ptr @element() {
; CHECK-LABEL: element:
; CHECK:         addl r14 = @ltoff(a), r1 ;;
; CHECK-NEXT:    ld8 r14 = [r14] ;;
; CHECK-NEXT:    adds r8 = 24, r14
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  ret ptr getelementptr (i64, ptr @a, i64 3)
}

; A pointer to a function is the address of its descriptor, and the entry
; holds the address of the one descriptor that every module uses.

define ptr @function() {
; CHECK-LABEL: function:
; CHECK:         addl r14 = @ltoff(@fptr(f)), r1 ;;
; CHECK-NEXT:    ld8 r8 = [r14]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  ret ptr @f
}

define ptr @itself() {
; CHECK-LABEL: itself:
; CHECK:         addl r14 = @ltoff(@fptr(itself)), r1 ;;
; CHECK-NEXT:    ld8 r8 = [r14]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  ret ptr @itself
}
