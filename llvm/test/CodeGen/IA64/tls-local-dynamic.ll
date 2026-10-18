; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=pic \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,DAG
; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=pic -global-isel \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,GISEL
; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=pic -O0 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0

; A variable of a thread that the module defines is at an offset the link
; editor knows from what the module gives the thread, and where that is
; __tls_get_addr tells, called with the module and an offset of nothing. The
; answer does not depend on the variable, so a function asks once on any path
; and adds the offset of each variable to it. A call that no other dominates
; stays, and so does one for a variable of another module, which is asked for
; with an offset of its own. Without optimization each access makes its call.

@a = internal thread_local(localdynamic) global i32 1
@b = internal thread_local(localdynamic) global i32 2
@c = thread_local global i32 3

declare void @ext()

; CHECK-LABEL: two:
; CHECK: addl r14 = @ltoff(@dtpmod(a)), r1
; CHECK: br.call.sptk.many b0 = __tls_get_addr
; CHECK: mov r3 = r8
; CHECK: addl r14 = @dtprel(a), r3
; CHECK-NEXT: addl r15 = @dtprel(b), r3
; CHECK-NOT: __tls_get_addr
; CHECK: br.ret.sptk.many b0

; O0-LABEL: two:
; O0: br.call.sptk.many b0 = __tls_get_addr
; O0: addl r14 = @dtprel(a), r3
; O0: br.call.sptk.many b0 = __tls_get_addr
; O0: addl r15 = @dtprel(b), r3
define i32 @two() {
  %x = load i32, ptr @a
  %y = load i32, ptr @b
  %z = add i32 %x, %y
  ret i32 %z
}

; What was returned is kept in a register that the call of ext preserves.
; CHECK-LABEL: around_a_call:
; CHECK: br.call.sptk.many b0 = __tls_get_addr
; CHECK: mov r3 = r8
; CHECK-NOT: __tls_get_addr
; CHECK: br.call.sptk.many b0 = ext
; CHECK-NOT: __tls_get_addr
; CHECK: br.ret.sptk.many b0
define i32 @around_a_call() {
  %x = load i32, ptr @a
  call void @ext()
  %y = load i32, ptr @b
  %z = add i32 %x, %y
  ret i32 %z
}

; The access on one side of a branch does not dominate the one after it.
; SelectionDAG leaves a call where each access is, and GlobalISel has the
; addresses of globals made in the entry block, where one call serves both.
; CHECK-LABEL: one_side:
; DAG: addl r14 = @ltoff(@dtpmod(a)), r1
; DAG: br.call.sptk.many b0 = __tls_get_addr
; DAG: {{^}}.LBB2_2:
; DAG: addl r14 = @ltoff(@dtpmod(b)), r1
; DAG: br.call.sptk.many b0 = __tls_get_addr
; GISEL: br.call.sptk.many b0 = __tls_get_addr
; GISEL-NOT: __tls_get_addr
; CHECK: br.ret.sptk.many b0
define i32 @one_side(i1 %p) {
entry:
  br i1 %p, label %then, label %end

then:
  store volatile i32 0, ptr @a
  br label %end

end:
  %y = load volatile i32, ptr @b
  ret i32 %y
}

; The access in the entry block dominates the two that follow it.
; CHECK-LABEL: both_sides:
; CHECK: br.call.sptk.many b0 = __tls_get_addr
; CHECK-NOT: __tls_get_addr
; CHECK: br.ret.sptk.many b0
define i32 @both_sides(i1 %p) {
entry:
  store volatile i32 0, ptr @a
  br i1 %p, label %then, label %end

then:
  store volatile i32 0, ptr @b
  br label %end

end:
  %y = load volatile i32, ptr @a
  ret i32 %y
}

; CHECK-LABEL: general:
; CHECK: addl r14 = @ltoff(@dtpmod(c)), r1
; CHECK: addl r14 = @ltoff(@dtprel(c)), r1
; CHECK: br.call.sptk.many b0 = __tls_get_addr
; CHECK: addl r14 = @ltoff(@dtpmod(a)), r1
; CHECK: br.call.sptk.many b0 = __tls_get_addr
; CHECK: addl r14 = @dtprel(a), r3
define i32 @general() {
  %x = load i32, ptr @c
  %y = load i32, ptr @a
  %z = add i32 %x, %y
  ret i32 %z
}
