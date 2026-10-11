; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A function is entered with the stacked registers its arguments are in and
; no others. One that needs more than the scratch registers takes locals,
; which it has to allocate, and the first local it has no use for receives
; the caller's frame state.

define i64 @leaf(i64 %a, i64 %b) {
; CHECK-LABEL: leaf:
; CHECK-NOT:     alloc
; CHECK:         add r8 = r32, r33
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = add i64 %a, %b
  ret i64 %r
}

; Twenty-five values at once: r14 to r31, r8 to r11 and r3 hold the first
; twenty-three, then r34, and the last can have r32, which is dead by then.
define void @many(ptr %p, ptr %q) {
; CHECK-LABEL: many:
; CHECK:         alloc r35 = ar.pfs, 2, 2, 0, 0
; CHECK-NEXT:    .body
; CHECK-NEXT:    ld8 r14 = [r32]
; CHECK:         ld8 r34 = [r32]
; CHECK-NEXT:    ld8 r32 = [r32] ;;
; CHECK:         st8 [r33] = r34
; CHECK-NEXT:    st8 [r33] = r32
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %v0 = load volatile i64, ptr %p
  %v1 = load volatile i64, ptr %p
  %v2 = load volatile i64, ptr %p
  %v3 = load volatile i64, ptr %p
  %v4 = load volatile i64, ptr %p
  %v5 = load volatile i64, ptr %p
  %v6 = load volatile i64, ptr %p
  %v7 = load volatile i64, ptr %p
  %v8 = load volatile i64, ptr %p
  %v9 = load volatile i64, ptr %p
  %v10 = load volatile i64, ptr %p
  %v11 = load volatile i64, ptr %p
  %v12 = load volatile i64, ptr %p
  %v13 = load volatile i64, ptr %p
  %v14 = load volatile i64, ptr %p
  %v15 = load volatile i64, ptr %p
  %v16 = load volatile i64, ptr %p
  %v17 = load volatile i64, ptr %p
  %v18 = load volatile i64, ptr %p
  %v19 = load volatile i64, ptr %p
  %v20 = load volatile i64, ptr %p
  %v21 = load volatile i64, ptr %p
  %v22 = load volatile i64, ptr %p
  %v23 = load volatile i64, ptr %p
  %v24 = load volatile i64, ptr %p
  store volatile i64 %v0, ptr %q
  store volatile i64 %v1, ptr %q
  store volatile i64 %v2, ptr %q
  store volatile i64 %v3, ptr %q
  store volatile i64 %v4, ptr %q
  store volatile i64 %v5, ptr %q
  store volatile i64 %v6, ptr %q
  store volatile i64 %v7, ptr %q
  store volatile i64 %v8, ptr %q
  store volatile i64 %v9, ptr %q
  store volatile i64 %v10, ptr %q
  store volatile i64 %v11, ptr %q
  store volatile i64 %v12, ptr %q
  store volatile i64 %v13, ptr %q
  store volatile i64 %v14, ptr %q
  store volatile i64 %v15, ptr %q
  store volatile i64 %v16, ptr %q
  store volatile i64 %v17, ptr %q
  store volatile i64 %v18, ptr %q
  store volatile i64 %v19, ptr %q
  store volatile i64 %v20, ptr %q
  store volatile i64 %v21, ptr %q
  store volatile i64 %v22, ptr %q
  store volatile i64 %v23, ptr %q
  store volatile i64 %v24, ptr %q
  ret void
}
