; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O0 < %s | FileCheck %s --check-prefix=O0

; The kernel may run a system call again from the registers its arguments
; came in, so the function has all eight of them in its frame and writes none.

declare i64 @g(i64, i64)

define i64 @f(i64 %a, i64 %b) "syscall-linkage" {
; CHECK-LABEL: f:
; CHECK:       alloc [[PFS:r[0-9]+]] = ar.pfs, 8, {{[0-9]+}}, 2, 0
; CHECK-NOT:   {{r3[2-9]}} =
; CHECK:       br.call.sptk.many b0 = g
; CHECK-NOT:   {{r3[2-9]}} =
; CHECK:       br.ret.sptk.many b0
; O0-LABEL:    f:
; O0:          alloc {{r[0-9]+}} = ar.pfs, 8, {{[0-9]+}}, 2, 0
; O0-NOT:      {{r3[2-9]}} =
; O0:          br.ret.sptk.many b0
  %a1 = add i64 %a, 1
  %r = call i64 @g(i64 %a1, i64 %b)
  %s = add i64 %a1, %r
  %t = mul i64 %s, %b
  ret i64 %t
}

; One that needs no frame of its own writes nothing either.
define i64 @leaf(i64 %a) "syscall-linkage" {
; CHECK-LABEL: leaf:
; CHECK-NOT:   alloc
; CHECK:       adds r8 = 1, r32
  %r = add i64 %a, 1
  ret i64 %r
}

; A callee that returned for it would have its arguments where these are.
define i64 @tail(i64 %a, i64 %b) "syscall-linkage" {
; CHECK-LABEL: tail:
; CHECK:       alloc {{r[0-9]+}} = ar.pfs, 8, {{[0-9]+}}, 2, 0
; CHECK-NOT:   {{r3[2-9]}} =
; CHECK:       br.call.sptk.many b0 = local
  %r = tail call i64 @local(i64 %b, i64 %a)
  ret i64 %r
}

define internal i64 @local(i64 %a, i64 %b) noinline {
; CHECK-LABEL: local:
; CHECK-NOT:   alloc
  %r = sub i64 %a, %b
  ret i64 %r
}
