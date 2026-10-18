; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -emulated-tls < %s | FileCheck %s

; Where the variables of a thread are emulated, the address of one is what
; __emutls_get_address returns for the variable that describes it, and an
; offset into it is added to that.

@t = thread_local global i32 5
@u = external thread_local global [4 x i64]

define i32 @get() {
; CHECK-LABEL: get:
; CHECK:       addl [[A:r[0-9]+]] = @ltoff(__emutls_v.t), r1
; CHECK:       ld8 r35 = {{\[}}[[A]]{{\]}}
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = __emutls_get_address ;;
; CHECK:       ld4 r8 = [r8]
  %v = load i32, ptr @t
  ret i32 %v
}

define ptr @offset() {
; CHECK-LABEL: offset:
; CHECK:       addl [[A:r[0-9]+]] = @ltoff(__emutls_v.u), r1
; CHECK:       ld8 r35 = {{\[}}[[A]]{{\]}}
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = __emutls_get_address ;;
; CHECK:       adds r8 = 16, r8
  ret ptr getelementptr (i8, ptr @u, i64 16)
}

; CHECK-LABEL: __emutls_v.t:
; CHECK:       data8.ua __emutls_t.t
