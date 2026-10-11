; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The address of where a result is to go that is returned in memory is in r8.
; It is not one of the arguments, and does not take the slot of one.

%big = type { [6 x i64] }

declare void @make(ptr sret(%big), i64)
declare dso_local void @make_local(ptr sret(%big), i64)

define void @callee(ptr sret(%big) %res, i64 %a) {
; CHECK-LABEL: callee:
; CHECK:         st8 [r8] = r32
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  store i64 %a, ptr %res
  ret void
}

define i64 @caller(i64 %a) {
; CHECK-LABEL: caller:
; CHECK:         mov r36 = r32 ;;
; CHECK-NEXT:    adds r32 = 16, r12 ;;
; CHECK-NEXT:    mov r8 = r32
; CHECK-NEXT:    br.call.sptk.many b0 = make ;;
  %res = alloca %big
  call void @make(ptr sret(%big) %res, i64 %a)
  %v = load i64, ptr %res
  ret i64 %v
}

; It is where the callee looks for it when the function hands on what it was
; given.

define void @forward(ptr sret(%big) %res, i64 %a) {
; CHECK-LABEL: forward:
; CHECK:         .body
; CHECK-NOT:     r8
; CHECK:         alloc r2 = ar.pfs, 0, 0, 1, 0
; CHECK-NEXT:    br.cond.sptk.many make_local ;;
  tail call void @make_local(ptr sret(%big) %res, i64 %a)
  ret void
}

; A result that the registers do not hold is returned the same way.

define i512 @wide(i512 %a) {
; CHECK-LABEL: wide:
; CHECK:         st8 [r8] = r32
  ret i512 %a
}
