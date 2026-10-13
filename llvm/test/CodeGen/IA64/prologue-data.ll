; RUN: split-file %s %t
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %t/bundle.ll | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %t/bundle.ll \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ
; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %t/word.ll -o /dev/null \
; RUN:   2>&1 | FileCheck %s --check-prefix=ERR

; Prologue data is where the function begins and its instructions come
; after it. A bundle of them is at a multiple of sixteen bytes, so the data
; has to be whole bundles, and anything else is refused: the assembler would
; not take what was written for it.

; ERR: LLVM ERROR: prologue data that is not a whole number of bundles is not supported on IA-64

;--- bundle.ll
define void @f() prologue [2 x i64] [i64 4294967296, i64 1125899906843136] {
; CHECK-LABEL: f:
; CHECK-NEXT:    data8.ua 4294967296
; CHECK-NEXT:    data8.ua 1125899906843136
; CHECK:         br.ret.sptk.many b0 ;;
; OBJ-LABEL: <f>:
; OBJ-NEXT:     0: {{.*}}nop.m
; OBJ:         10: {{.*}}nop.m 0 // [MIB]
; OBJ:         1c: {{.*}}br.ret.sptk.many b0
  ret void
}

;--- word.ll
define void @f() prologue i32 1 {
  ret void
}
