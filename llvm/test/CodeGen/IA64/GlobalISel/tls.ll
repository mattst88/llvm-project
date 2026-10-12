; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -relocation-model=pic < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,PIC,PIC22
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -relocation-model=pic -tls-size=14 < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,PIC,PIC14
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -relocation-model=pic -tls-size=64 < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,PIC,PIC64
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -relocation-model=static < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,STATIC

; The address of a variable of a thread is an offset from the thread pointer
; where the variable is the program's own, and what __tls_get_addr returns
; where it may be a module's that was loaded later. A module that knows the
; variable is its own calls with no offset and adds one the link editor
; knows, by the instruction that the size of the offset allows.

@gd = external thread_local global i32
@ld = internal thread_local global i32 0
@ie = external thread_local(initialexec) global i32
@le = internal thread_local(localexec) global i32 0

; CHECK-LABEL: f_gd:
; PIC:         addl [[M:r[0-9]+]] = @ltoff(@dtpmod(gd)), r1
; PIC:         ld8 r35 = {{\[}}[[M]]{{\]}}
; PIC:         addl [[O:r[0-9]+]] = @ltoff(@dtprel(gd)), r1
; PIC:         ld8 r36 = {{\[}}[[O]]{{\]}}
; PIC:         br.call.sptk.many b0 = __tls_get_addr
; PIC:         mov r1 = r32
; STATIC:      addl [[O:r[0-9]+]] = @ltoff(@tprel(gd)), r1
; STATIC:      ld8 [[V:r[0-9]+]] = {{\[}}[[O]]{{\]}}
; STATIC:      add r8 = [[V]], r13
define ptr @f_gd() {
  ret ptr @gd
}

; CHECK-LABEL: f_ld:
; PIC:         addl [[M:r[0-9]+]] = @ltoff(@dtpmod(ld)), r1
; PIC:         mov r36 = r0
; PIC:         ld8 r35 = {{\[}}[[M]]{{\]}}
; PIC:         br.call.sptk.many b0 = __tls_get_addr
; PIC22:       mov r3 = r8
; PIC22:       addl r8 = @dtprel(ld), r3
; PIC14:       adds r8 = @dtprel(ld), r8
; PIC64:       addl [[O:r[0-9]+]] = @ltoff(@dtprel(ld)), r1
; PIC64:       ld8 [[V:r[0-9]+]] = {{\[}}[[O]]{{\]}}
; PIC64:       add r8 = r8, [[V]]
; STATIC:      mov r3 = r13
; STATIC:      addl r8 = @tprel(ld), r3
define ptr @f_ld() {
  ret ptr @ld
}

; CHECK-LABEL: f_ie:
; CHECK:       addl [[O:r[0-9]+]] = @ltoff(@tprel(ie)), r1
; CHECK:       ld8 [[V:r[0-9]+]] = {{\[}}[[O]]{{\]}}
; CHECK:       add r8 = [[V]], r13
define ptr @f_ie() {
  ret ptr @ie
}

; CHECK-LABEL: f_le:
; PIC22:       mov r3 = r13
; PIC22:       addl r8 = @tprel(le), r3
; PIC14:       adds r8 = @tprel(le), r13
; PIC64:       movl [[O:r[0-9]+]] = @tprel(le)
; PIC64:       add r8 = [[O]], r13
; STATIC:      mov r3 = r13
; STATIC:      addl r8 = @tprel(le), r3
define ptr @f_le() {
  ret ptr @le
}
