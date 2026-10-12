; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %t/kcfi.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=KCFI
; RUN: not --crash llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %t/ptrauth.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PTRAUTH
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -global-isel \
; RUN:   -global-isel-abort=2 < %t/unknown.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=UNKNOWN

; A call with an operand bundle that asks for a check there is no code for
; is not made without the check: it is left to SelectionDAG, which refuses
; it.

; KCFI: warning: Instruction selection used fallback path for f
; KCFI: LLVM ERROR: Target doesn't support calls with kcfi operand bundles.
; PTRAUTH: warning: Instruction selection used fallback path for f
; PTRAUTH: LLVM ERROR: This target doesn't support calls with ptrauth operand bundles.
; UNKNOWN: warning: Instruction selection used fallback path for f
; UNKNOWN: LLVM ERROR: cannot lower calls with arbitrary operand bundles: gc-live

;--- kcfi.ll
define void @f(ptr %p) {
  call void %p(i64 1) [ "kcfi"(i32 1234) ]
  ret void
}

;--- ptrauth.ll
define void @f(ptr %p) {
  call void %p(i64 1) [ "ptrauth"(i32 0, i64 42) ]
  ret void
}

;--- unknown.ll
declare void @g(i64)

define void @f(ptr %p) {
  call void @g(i64 1) [ "gc-live"(ptr %p) ]
  ret void
}
