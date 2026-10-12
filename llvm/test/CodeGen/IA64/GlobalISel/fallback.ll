; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel < %s 2>&1 \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DAG

; What GlobalISel does not do yet is SelectionDAG's, a function at a time,
; and it says which. Nothing is said when it was not asked for.

; CHECK: warning: Instruction selection used fallback path for extended
; CHECK-NOT: warning:
; DAG-NOT: warning:

define i64 @plain(i64 %a) {
; CHECK-LABEL: plain:
; CHECK:       mov r8 = r32
; DAG-LABEL:   plain:
  ret i64 %a
}

define x86_fp80 @extended(x86_fp80 %a) {
; CHECK-LABEL: extended:
; CHECK:       br.ret.sptk.many b0 ;;
; DAG-LABEL:   extended:
  ret x86_fp80 %a
}
