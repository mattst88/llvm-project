; RUN: opt < %s -passes=rel-lookup-table-converter -mtriple=ia64-unknown-linux-gnu -relocation-model=pic -S | FileCheck %s

; The table's entries point at tables of pointers, which go in the data
; segment, out of reach of a 32-bit offset from .rodata. The table stays one
; of pointers.

target datalayout = "e-m:e-p:64:64-i64:64-i128:128-f80:128-n32:64-S128"

@x = internal global i32 0
@a = internal constant [1 x ptr] [ptr @x]
@b = internal constant [1 x ptr] [ptr @x]

@switch.table.f = private unnamed_addr constant [2 x ptr] [ptr @a, ptr @b], align 8

; CHECK: @switch.table.f = private unnamed_addr constant [2 x ptr] [ptr @a, ptr @b]
; CHECK-LABEL: define ptr @f(
; CHECK: %switch.gep = getelementptr inbounds [2 x ptr], ptr @switch.table.f, i64 0, i64 %i
; CHECK: %switch.load = load ptr, ptr %switch.gep
define ptr @f(i64 %i) {
  %switch.gep = getelementptr inbounds [2 x ptr], ptr @switch.table.f, i64 0, i64 %i
  %switch.load = load ptr, ptr %switch.gep, align 8
  ret ptr %switch.load
}
