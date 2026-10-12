; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel < %s 2>&1 \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DAG

; What GlobalISel does not do yet is SelectionDAG's, a function at a time,
; and it says which. Nothing is said when it was not asked for.

; CHECK: warning: Instruction selection used fallback path for variadic
; CHECK-NOT: warning:
; DAG-NOT: warning:

define i64 @plain(i64 %a) {
; CHECK-LABEL: plain:
; CHECK:       mov r8 = r32
; DAG-LABEL:   plain:
  ret i64 %a
}

declare void @llvm.va_start(ptr)

define i64 @variadic(i64 %a, ...) {
; CHECK-LABEL: variadic:
; CHECK:       br.ret.sptk.many b0 ;;
; DAG-LABEL:   variadic:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %r = va_arg ptr %ap, i64
  ret i64 %r
}
