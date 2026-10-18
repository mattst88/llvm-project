; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: opt -mtriple=ia64-unknown-linux-gnu -S \
; RUN:   -passes='ee-instrument<post-inline>' < %s \
; RUN:   | FileCheck %s --check-prefix=IR

; The profiler is called before the function has a frame, and is given
; ar.pfs, gp and the return link to put back, and no counter.

declare i64 @g(i64)

define i64 @leaf(i64 %a) #0 {
; CHECK-LABEL: leaf:
; CHECK:         .prologue
; CHECK-NEXT:    .save ar.pfs, r40
; CHECK-NEXT:    alloc r40 = ar.pfs, 8, 0, 4, 0
; CHECK-NEXT:    mov r43 = r0 ;;
; CHECK-NEXT:    .save rp, r42
; CHECK-NEXT:    mov r42 = b0
; CHECK-NEXT:    .body
; CHECK-NEXT:    mov r41 = r1
; CHECK-NEXT:    br.call.sptk.many b0 = _mcount ;;
; CHECK-NEXT:    .prologue
; CHECK-NEXT:    .body
; CHECK:         adds r8 = 1, r32
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %r = add i64 %a, 1
  ret i64 %r
}

define i64 @caller(i64 %a) #0 {
; CHECK-LABEL: caller:
; CHECK:         alloc r40 = ar.pfs, 8, 0, 4, 0
; CHECK:         br.call.sptk.many b0 = _mcount ;;
; CHECK-NEXT:    .prologue
; CHECK:         .save ar.pfs, r34
; CHECK-NEXT:    .mmf
; CHECK-NEXT:    alloc r34 = ar.pfs, 1, 3, 1, 0
; CHECK:         br.call.sptk.many b0 = g ;;
  %r = call i64 @g(i64 %a)
  %s = add i64 %r, 1
  ret i64 %s
}

define i64 @plain(i64 %a) {
; CHECK-LABEL: plain:
; CHECK-NOT:     _mcount
; CHECK:         .endp
  ret i64 %a
}

; A hook that is not the profiler's is an ordinary function, which returns
; as one does and puts nothing back, so it is called from inside the frame.
define i64 @bare(i64 %a) #1 {
; CHECK-LABEL: bare:
; CHECK-NOT:     alloc r40
; CHECK:         alloc r{{[0-9]+}} = ar.pfs, 1, 3, 0, 0
; CHECK:         .body
; CHECK:         br.call.sptk.many b0 = __cyg_profile_func_enter_bare ;;
; CHECK:         br.ret.sptk.many b0 ;;
; IR-LABEL: define i64 @bare(
; IR-NEXT:    call void @__cyg_profile_func_enter_bare()
; IR-NEXT:    ret i64 %a
  ret i64 %a
}

; IR:       attributes #0 = { "ia64-instrument-function-entry"="_mcount" }

attributes #1 = { "instrument-function-entry-inlined"="__cyg_profile_func_enter_bare" }
attributes #0 = { "instrument-function-entry-inlined"="_mcount" }
