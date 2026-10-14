; RUN: opt -passes=instsimplify -S < %s | FileCheck %s

; The initializer of a "function-descriptors" global does not describe its
; contents, so a load from it is not folded.

@vtable = constant [2 x ptr] [ptr @f, ptr null] #0
@table = constant [2 x ptr] [ptr @f, ptr null]

declare void @f()

define ptr @load_vtable() {
; CHECK-LABEL: @load_vtable(
; CHECK-NEXT: [[P:%.*]] = load ptr, ptr @vtable
; CHECK-NEXT: ret ptr [[P]]
  %p = load ptr, ptr @vtable
  ret ptr %p
}

define ptr @load_vtable_second() {
; CHECK-LABEL: @load_vtable_second(
; CHECK-NEXT: [[P:%.*]] = load ptr, ptr getelementptr (i8, ptr @vtable, i64 8)
; CHECK-NEXT: ret ptr [[P]]
  %p = load ptr, ptr getelementptr (i8, ptr @vtable, i64 8)
  ret ptr %p
}

define ptr @load_table() {
; CHECK-LABEL: @load_table(
; CHECK-NEXT: ret ptr @f
  %p = load ptr, ptr @table
  ret ptr %p
}

attributes #0 = { "function-descriptors" }
