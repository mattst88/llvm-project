; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   -ia64-speculate-same-object=false | FileCheck %s --check-prefix=PAST

; Two counters of one table, indexed by loaded bytes. Bytes repeat often
; enough that the store to the first counter would invalidate an early load
; of the second, which then waits for the store, so it stays below it.
define void @histogram(ptr %t, ptr %p) {
; CHECK-LABEL: histogram:
; CHECK-NOT:   .sa
; CHECK-NOT:   .c.clr
; CHECK:       .endp histogram
; PAST-LABEL:  histogram:
; PAST:        ld4.sa
; PAST:        st4
; PAST:        ld4.c.clr
entry:
  %b0 = load i8, ptr %p
  %p1 = getelementptr i8, ptr %p, i64 1
  %b1 = load i8, ptr %p1
  %i0 = zext i8 %b0 to i64
  %i1 = zext i8 %b1 to i64
  %a0 = getelementptr i32, ptr %t, i64 %i0
  %v0 = load i32, ptr %a0
  %n0 = add i32 %v0, 1
  store i32 %n0, ptr %a0
  %a1 = getelementptr i32, ptr %t, i64 %i1
  %v1 = load i32, ptr %a1
  %n1 = add i32 %v1, 1
  store i32 %n1, ptr %a1
  ret void
}
