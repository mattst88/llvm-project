; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %s 2>&1 | FileCheck %s

; A call that has to be a branch and cannot be one is an error. The callee
; here may be in another module, with another gp.

; CHECK: LLVM ERROR: failed to perform tail call elimination on a call site marked musttail

declare i64 @extern(i64)

define i64 @f(i64 %a) {
  %r = musttail call i64 @extern(i64 %a)
  ret i64 %r
}
