; REQUIRES: ia64
;; Test we can infer the e_machine value EM_IA_64 from a bitcode file.

; RUN: llvm-as %s -o %t.bc
; RUN: ld.lld %t.bc -o %t
; RUN: llvm-readobj -h %t | FileCheck %s

; CHECK:   Class: 64-bit
; CHECK:   DataEncoding: LittleEndian
; CHECK: Machine: EM_IA_64

target datalayout = "e-m:e-p:64:64-i64:64-i128:128-f80:128-n64-S128"
target triple = "ia64-unknown-linux-gnu"

define void @_start() {
  ret void
}
