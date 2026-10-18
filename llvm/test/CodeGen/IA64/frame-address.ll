; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

declare ptr @llvm.returnaddress(i32)
declare ptr @llvm.frameaddress(i32)
declare void @g(ptr)

; The return link is in b0 as the function is entered.
define ptr @return_address() {
; CHECK-LABEL: return_address:
; CHECK:       mov r8 = b0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

define ptr @return_address_call() {
; CHECK-LABEL: return_address_call:
; CHECK:       mov r32 = b0
; CHECK:       br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r8 = r32
  call void @g(ptr null)
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

; The address of the frame is the stack pointer the function was entered
; with, which a local keeps.
define ptr @frame_address() {
; CHECK-LABEL: frame_address:
; CHECK:       alloc r32 = ar.pfs, 0, 2, 0, 0
; CHECK-NEXT:  .vframe r33
; CHECK-NEXT:  mov r33 = r12 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov r8 = r33
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  mov r12 = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call ptr @llvm.frameaddress(i32 0)
  ret ptr %r
}

define ptr @frame_address_call() {
; CHECK-LABEL: frame_address_call:
; CHECK:       alloc r33 = ar.pfs, 0, 4, 1, 0
; CHECK-NEXT:  .vframe r35
; CHECK-NEXT:  mov r35 = r12
; CHECK-NEXT:  adds r12 = -48, r12
; CHECK-NEXT:  mov r32 = r1 ;;
; CHECK-NEXT:  adds r36 = -24, r35
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r8 = r35
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  mov r12 = r35
; CHECK-NEXT:  mov b0 = r34
; CHECK-NEXT:  mov r1 = r32
  %b = alloca [40 x i8]
  call void @g(ptr %b)
  %r = call ptr @llvm.frameaddress(i32 0)
  ret ptr %r
}

; Only an unwinder can say what those of a caller are.
define ptr @return_address_caller() {
; CHECK-LABEL: return_address_caller:
; CHECK:       mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call ptr @llvm.returnaddress(i32 1)
  ret ptr %r
}

define ptr @frame_address_caller() {
; CHECK-LABEL: frame_address_caller:
; CHECK:       mov r8 = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %r = call ptr @llvm.frameaddress(i32 1)
  ret ptr %r
}

declare ptr @llvm.sponentry.p0()
declare ptr @llvm.eh.dwarf.cfa(i32)

; The stack pointer on entry is the same address.
define ptr @sp_on_entry() {
; CHECK-LABEL: sp_on_entry:
; CHECK:       .vframe r33
; CHECK-NEXT:  mov r33 = r12 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov r8 = r33
  %r = call ptr @llvm.sponentry.p0()
  ret ptr %r
}

; The canonical frame address is past the scratch area of sixteen bytes that
; the caller left there, where the arguments in memory begin. The operand,
; which is an i32, is added to it.
define ptr @dwarf_cfa() {
; CHECK-LABEL: dwarf_cfa:
; CHECK:       .vframe r33
; CHECK-NEXT:  mov r33 = r12 ;;
; CHECK:       adds r8 = 16, r{{[0-9]+}}
; CHECK-NEXT:  .restore sp
  %r = call ptr @llvm.eh.dwarf.cfa(i32 0)
  ret ptr %r
}

define ptr @dwarf_cfa_offset(i64, i64, i64, i64, i64, i64, i64, i64, i64 %a) {
; CHECK-LABEL: dwarf_cfa_offset:
; CHECK:       adds r8 = 24, r{{[0-9]+}}
; CHECK-NEXT:  .restore sp
  %r = call ptr @llvm.eh.dwarf.cfa(i32 8)
  ret ptr %r
}
