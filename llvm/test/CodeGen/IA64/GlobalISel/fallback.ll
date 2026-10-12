; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel < %s 2>&1 \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DAG

; What GlobalISel does not do yet is SelectionDAG's, a function at a time,
; and it says which. Nothing is said when it was not asked for.

; CHECK: warning: Instruction selection used fallback path for must
; CHECK: warning: Instruction selection used fallback path for elements
; CHECK-NOT: warning:
; DAG-NOT: warning:

define dso_local i64 @plain(i64 %a) {
; CHECK-LABEL: plain:
; CHECK:       mov r8 = r32
; DAG-LABEL:   plain:
  ret i64 %a
}

define i64 @must(i64 %a) {
; CHECK-LABEL: must:
; CHECK:       br.cond.sptk.many plain ;;
; DAG-LABEL:   must:
  %r = musttail call i64 @plain(i64 %a)
  ret i64 %r
}

declare void @any(<2 x float>, ...)

; A floating-point argument that a callee with a variable number of them
; has named is in two places, and the second is one where a value would be
; looked for and an element of a vector is not.
define void @elements(<2 x float> %a) {
; CHECK-LABEL: elements:
; CHECK:       br.call.sptk.many b0 = any ;;
; DAG-LABEL:   elements:
  call void (<2 x float>, ...) @any(<2 x float> %a)
  ret void
}
