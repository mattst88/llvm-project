; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The shape of CPython's _Py_Dealloc: an indirect call through a function
; pointer loaded at the top, behind a rarely taken call to a tracer. The
; entry point in the function descriptor is loaded with ld8.sa before the
; branch around the tracer, on the edge that nearly always reaches the call,
; and its post-increment load is split. The tracer's path issues its own
; ld8.sa after its call, which the check would not survive.

@tracer = external global ptr
@tracer_data = external global ptr

; CHECK-LABEL: dealloc:
; CHECK: adds [[SLOT:r[0-9]+]] = 48,
; CHECK: ld8 [[FN:r[0-9]+]] = [[[SLOT]]]
; CHECK: ld8.sa [[ENTRY:r[0-9]+]] = [[[FN]]]
; CHECK: br.cond
; CHECK: .LBB0_[[JOIN:[0-9]+]]:
; CHECK-NEXT: ld8.c.clr [[ENTRY]] = [[[FN]]]
; CHECK: mov b6 = [[ENTRY]]
; CHECK: br.call.sptk.many b0 = b6
; CHECK: br.ret
; CHECK: br.call.sptk.many b0 = b6
; CHECK-NEXT: ld8.sa [[ENTRY]] = [[[FN]]]
; CHECK: br.cond.sptk.few .LBB0_[[JOIN]]
define void @dealloc(ptr %op) {
entry:
  %ob_type = getelementptr inbounds nuw i8, ptr %op, i64 8
  %t = load ptr, ptr %ob_type, align 8
  %tp_dealloc = getelementptr inbounds nuw i8, ptr %t, i64 48
  %fn = load ptr, ptr %tp_dealloc, align 8
  %tr = load ptr, ptr @tracer, align 8
  %none = icmp eq ptr %tr, null
  br i1 %none, label %call, label %trace, !prof !0

trace:
  %data = load ptr, ptr @tracer_data, align 8
  tail call void %tr(ptr %op, i32 1, ptr %data)
  br label %call

call:
  tail call void %fn(ptr %op)
  ret void
}

!0 = !{!"branch_weights", i32 2000, i32 1}
