; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A count of the compares that hold is the count plus one, computed beside
; the compare, moved into the count under the compare's predicate: not a 0
; or 1 moved under it and then added, one more instruction on the chain.
define i32 @count(double %x, double %y, i32 %n) {
; CHECK-LABEL: count:
; CHECK-DAG:   mov r8 = r34
; CHECK-DAG:   adds [[INC:r[0-9]+]] = 1, r34
; CHECK-DAG:   fcmp.le p6, p7 = f8, f9
; CHECK:       (p6) mov r8 = [[INC]]
; CHECK-NEXT:  br.ret.sptk.many b0
  %c = fcmp ole double %x, %y
  %z = zext i1 %c to i32
  %r = add i32 %n, %z
  ret i32 %r
}

; Subtracting the compare counts down.
define i64 @down(i64 %a, i64 %b, i64 %n) {
; CHECK-LABEL: down:
; CHECK-DAG:   mov r8 = r34
; CHECK-DAG:   adds [[DEC:r[0-9]+]] = -1, r34
; CHECK-DAG:   cmp.lt p6, p7 = r32, r33
; CHECK:       (p6) mov r8 = [[DEC]]
; CHECK-NEXT:  br.ret.sptk.many b0
  %c = icmp slt i64 %a, %b
  %z = zext i1 %c to i64
  %r = sub i64 %n, %z
  ret i64 %r
}
