; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; The list of the arguments with no names is the address of the next of
; them, which at first is where the register that had the first was written.

declare void @v(i32, double, ...)
declare void @llvm.va_start(ptr)
declare void @llvm.va_copy(ptr, ptr)

define i64 @second(i64 %a, ...) {
; CHECK-LABEL: second:
; CHECK:       alloc r40 = ar.pfs, 8, 2, 0, 0
; CHECK:       mov r41 = ar.unat
; CHECK-DAG:   st8.spill [{{r[0-9]+}}] = r33
; CHECK-DAG:   st8.spill [{{r[0-9]+}}] = r39
; CHECK:       mov ar.unat = r41
; CHECK:       br.ret.sptk.many b0 ;;
  %ap = alloca ptr
  %aq = alloca ptr
  call void @llvm.va_start(ptr %ap)
  call void @llvm.va_copy(ptr %aq, ptr %ap)
  %p = load ptr, ptr %aq
  %q = getelementptr i8, ptr %p, i64 8
  %r = load i64, ptr %q
  ret i64 %r
}

; The double the callee has named is in f8 and in the second slot, and the
; one it has not is in the third alone.
define void @caller(double %d, double %e) {
; CHECK-LABEL: caller:
; CHECK-DAG:   getf.d r{{[0-9]+}} = f8
; CHECK-DAG:   getf.d r{{[0-9]+}} = f9
; CHECK:       br.call.sptk.many b0 = v
  call void (i32, double, ...) @v(i32 1, double %d, double %e)
  ret void
}
