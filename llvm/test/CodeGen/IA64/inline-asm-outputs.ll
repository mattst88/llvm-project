; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s

; The output registers go by the numbers r120 to r127 until the frame is laid
; out, and assembly in the source that names one gets the register it turns
; out to be. Naming one makes it part of the frame.

define i64 @outputs(i64 %a, i64 %b) {
; CHECK-LABEL: outputs:
; CHECK:       alloc r34 = ar.pfs, 2, 1, 3, 0
; CHECK-DAG:   mov r35 = r32
; CHECK-DAG:   mov r36 = r33
; CHECK:       add r8 = r35, r36
  %r = call i64 asm "add $0 = $1, $2", "={r8},{r120},{r121},~{r122}"(i64 %a, i64 %b)
  ret i64 %r
}
