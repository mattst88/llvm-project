; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   -o /dev/null 2>&1 | FileCheck %s

; A function that is to begin with an instruction a short jump can be
; written over has no such instruction to begin with, and is refused.
; "patchable-function-entry" is what leaves room in a function here.

; CHECK: LLVM ERROR: the "patchable-function" attribute is not supported on IA-64

define void @f() "patchable-function"="prologue-short-redirect" {
  ret void
}
