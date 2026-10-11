; RUN: split-file %s %t
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %t/none.ll \
; RUN:   | FileCheck %s --check-prefix=NONE
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %t/sixteen.ll \
; RUN:   | FileCheck %s --check-prefix=SIXTEEN

; A module says how large the largest object that is short data is. With a
; limit of nothing no object is short data, not even one that another module
; defines, and each is at an offset from the global pointer that takes a
; movl to hold.

;--- none.ll
@word = dso_local global i64 5
@zero = dso_local global i32 0
@hidden = external hidden global i64

define i64 @read() {
; NONE-LABEL: read:
; NONE-DAG:  movl r{{[0-9]+}} = @gprel(zero)
; NONE-DAG:  movl r{{[0-9]+}} = @gprel(word)
; NONE-DAG:  movl r{{[0-9]+}} = @gprel(hidden)
  %a = load i64, ptr @word
  %b = load i32, ptr @zero
  %c = load i64, ptr @hidden
  %d = zext i32 %b to i64
  %e = add i64 %a, %d
  %f = add i64 %e, %c
  ret i64 %f
}

; NONE-NOT: .sdata
; NONE-NOT: .sbss
; NONE:     .data
; NONE:     word:
; NONE:     .bss
; NONE:     zero:

!llvm.module.flags = !{!0}
!0 = !{i32 8, !"SmallDataLimit", i32 0}

;--- sixteen.ll
@pair = dso_local global [2 x i64] [i64 1, i64 2]
@three = dso_local global [3 x i64] zeroinitializer

define i64 @read() {
; SIXTEEN-LABEL: read:
; SIXTEEN-DAG:  movl r{{[0-9]+}} = @gprel(three+8)
; SIXTEEN-DAG:  addl r{{[0-9]+}} = @gprel(pair+8), r1
  %a = load i64, ptr getelementptr (i64, ptr @pair, i64 1)
  %b = load i64, ptr getelementptr (i64, ptr @three, i64 1)
  %c = add i64 %a, %b
  ret i64 %c
}

; SIXTEEN: .section .sdata,"aws",@progbits
; SIXTEEN: pair:
; SIXTEEN: .bss
; SIXTEEN: three:

!llvm.module.flags = !{!0}
!0 = !{i32 8, !"SmallDataLimit", i32 16}
