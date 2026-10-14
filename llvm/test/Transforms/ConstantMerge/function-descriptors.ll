; RUN: opt -passes=constmerge -S < %s | FileCheck %s

; The initializer of a "function-descriptors" global does not describe its
; contents, so such a global is not merged with anything.

; CHECK-DAG: @vtable1 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null] #0
; CHECK-DAG: @vtable2 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null] #0
; CHECK-DAG: @table2 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null]{{$}}
; CHECK-NOT: @table1

@vtable1 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null] #0
@table1 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null]
@vtable2 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null] #0
@table2 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr null]

declare void @f()

define ptr @get_vtable1() {
; CHECK-LABEL: @get_vtable1(
; CHECK: ret ptr @vtable1
  ret ptr @vtable1
}

define ptr @get_table1() {
; CHECK-LABEL: @get_table1(
; CHECK: ret ptr @table2
  ret ptr @table1
}

define ptr @get_vtable2() {
; CHECK-LABEL: @get_vtable2(
; CHECK: ret ptr @vtable2
  ret ptr @vtable2
}

define ptr @get_table2() {
; CHECK-LABEL: @get_table2(
; CHECK: ret ptr @table2
  ret ptr @table2
}

attributes #0 = { "function-descriptors" }
