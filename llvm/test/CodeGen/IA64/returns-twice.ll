; RUN: llc -mtriple=ia64 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64 -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR

; A function that returns twice comes back the second time with the stacked
; registers of its caller as they were last left, and with the preserved
; registers as they were at the call. What is needed after the call, the
; global pointer included, is kept in the preserved ones.
; The global pointer is put back by a copy after the call, and not from an
; operand of the call, which could be in a register that the call changes.

; MIR-LABEL: name: twice
; MIR:         CALL @setjmp, $r0, {{.*}}csr_ia64_returnstwice
; MIR:         $r1 = COPY %[[GP:[0-9]+]]
; MIR:         CALL @use, %[[GP]], {{.*}}csr_ia64_call
; MIR-LABEL: name: once
; MIR:         CALL @ordinary, %[[GP:[0-9]+]], {{.*}}csr_ia64_call
; MIR-NOT:     $r1 = COPY
; MIR:         CALL @use, %[[GP]], {{.*}}csr_ia64_call

declare i32 @setjmp(ptr) returns_twice
declare i32 @ordinary(ptr)
declare void @use(i64, i64)

define void @twice(ptr %buf, i64 %a, i64 %b) {
; CHECK-LABEL: twice:
; CHECK:         mov r[[GP:[4-7]]] = r1
; CHECK-DAG:     mov r[[B:[4-7]]] = r34
; CHECK-DAG:     mov r[[A:[4-7]]] = r33
; CHECK:         br.call.sptk.many b0 = setjmp
; CHECK-NEXT:    mov r1 = r[[GP]]
; CHECK-NEXT:    mov r38 = r[[A]]
; CHECK-NEXT:    mov r39 = r[[B]]
; CHECK-NEXT:    br.call.sptk.many b0 = use
  %r = call i32 @setjmp(ptr %buf)
  call void @use(i64 %a, i64 %b)
  ret void
}

define void @once(ptr %buf, i64 %a, i64 %b) {
; CHECK-LABEL: once:
; CHECK:         mov r[[GP:3[2-9]]] = r1
; CHECK:         br.call.sptk.many b0 = ordinary
; CHECK-NEXT:    mov r1 = r[[GP]]
; CHECK-NEXT:    mov r38 = r33
; CHECK-NEXT:    mov r39 = r34
; CHECK:         br.call.sptk.many b0 = use
  %r = call i32 @ordinary(ptr %buf)
  call void @use(i64 %a, i64 %b)
  ret void
}
