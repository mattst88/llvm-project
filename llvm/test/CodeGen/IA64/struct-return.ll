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
; CHECK-NEXT:    .mmi
; CHECK-NEXT:    adds r32 = 16, r12
; CHECK-NEXT:    mov r33 = r1 ;;
; CHECK-NEXT:    mov r8 = r32
; CHECK-NEXT:    .save rp, r35
; CHECK-NEXT:    mov r35 = b0
; CHECK-NEXT:    .body
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
; CHECK:         br.cond.sptk.many make_local ;;
  tail call void @make_local(ptr sret(%big) %res, i64 %a)
  ret void
}

; A result that the registers do not hold is returned the same way.

define i512 @wide(i512 %a) {
; CHECK-LABEL: wide:
; CHECK-NOT:     mov
; CHECK:         st8 [r8] = r32 ;;
  ret i512 %a
}

; The function returns the address it was given in r8, whatever else it has
; had there.

define void @keeps(ptr sret(%big) %res, i64 %a) {
; CHECK-LABEL: keeps:
; CHECK:         mov r[[SAVED:[0-9]+]] = r8
; CHECK:         br.call.sptk.many b0 = ext ;;
; CHECK:         mov r[[V:[0-9]+]] = r8
; CHECK:         mov r8 = r[[SAVED]]
; CHECK:         st8 [r[[SAVED]]] = r[[V]]
; CHECK:         br.ret.sptk.many b0 ;;
  %v = call i64 @ext(i64 %a)
  store i64 %v, ptr %res
  ret void
}

declare i64 @ext(i64)

; The C++ ABI makes the address the first argument where the result has a
; constructor or a destructor to run, which a front end marks as in a
; register. It is returned in r8 all the same.

declare void @construct(ptr sret(%big) inreg, i64)

define void @constructed(ptr sret(%big) inreg %res, i64 %a) {
; CHECK-LABEL: constructed:
; CHECK:         mov r8 = r32
; CHECK-NEXT:    st8 [r32] = r33
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  store i64 %a, ptr %res
  ret void
}

define i64 @constructs(i64 %a) {
; CHECK-LABEL: constructs:
; CHECK:         adds r[[RES:[0-9]+]] = 16, r12
; CHECK:         mov r36 = r[[RES]]
; CHECK:         br.call.sptk.many b0 = construct ;;
  %res = alloca %big
  call void @construct(ptr sret(%big) inreg %res, i64 %a)
  %v = load i64, ptr %res
  ret i64 %v
}
