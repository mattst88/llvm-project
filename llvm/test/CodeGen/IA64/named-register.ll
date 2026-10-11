; RUN: llc -mtriple=ia64-unknown-linux-gnu -mattr=+reserve-r5 < %s | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -filetype=null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ERR
; RUN: sed 's/"r5"/"f2"/' %s \
; RUN:   | not llc -mtriple=ia64-unknown-linux-gnu -filetype=null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BAD

; A global register variable is the register it names. The C library keeps the
; thread pointer in one, and reads and writes it. The register has to be one
; the allocator leaves alone: the conventions reserve some, and reserve-r5 is
; how the user reserves another.

; ERR: register "r5" is not reserved, so no variable can be kept in it
; BAD: invalid register name "f2"

; CHECK-LABEL: self:
; CHECK: mov r8 = r13
define i64 @self() {
  %v = call i64 @llvm.read_register.i64(metadata !0)
  ret i64 %v
}

; CHECK-LABEL: set_self:
; CHECK: mov r13 = r32
define void @set_self(i64 %v) {
  call void @llvm.write_register.i64(metadata !0, i64 %v)
  ret void
}

; CHECK-LABEL: stack:
; CHECK: mov r8 = r12
define i64 @stack() {
  %v = call i64 @llvm.read_register.i64(metadata !1)
  ret i64 %v
}

; CHECK-LABEL: global:
; CHECK: mov r8 = r1
define i64 @global() {
  %v = call i64 @llvm.read_register.i64(metadata !2)
  ret i64 %v
}

; CHECK-LABEL: mine:
; CHECK: mov r8 = r5
define i64 @mine() {
  %v = call i64 @llvm.read_register.i64(metadata !3)
  ret i64 %v
}

declare i64 @llvm.read_register.i64(metadata)
declare void @llvm.write_register.i64(metadata, i64)

!0 = !{!"r13"}
!1 = !{!"sp"}
!2 = !{!"gp"}
!3 = !{!"r5"}
