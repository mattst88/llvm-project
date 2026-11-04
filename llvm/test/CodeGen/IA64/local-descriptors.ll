; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s \
; RUN:   | FileCheck %s

; A function local to the module gets a descriptor of its own in .data, beside
; the tables that point to it, and every use of its address but a call names
; that descriptor. A function other modules can see, one in a constructor
; list and one with an alias keep the linker's.

declare void @extern()

define internal i32 @local(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

define internal void @aliased() {
  ret void
}

@alias = internal alias void (), ptr @aliased

define internal void @ctor() {
  ret void
}

define void @global() {
  ret void
}

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]

@table = global [4 x ptr] [ptr @local, ptr @extern, ptr @aliased, ptr @global]

; CHECK-LABEL: get:
; CHECK:         @gprel(.Llocal.desc)
; CHECK-NOT:     @fptr(local)
define ptr @get() {
  ret ptr @local
}

; A direct call stays a call to the code.
; CHECK-LABEL: call:
; CHECK:         br.call.sptk.many b0 = local
define i32 @call(i32 %x) {
  %r = call i32 @local(i32 %x)
  ret i32 %r
}

; CHECK:         .section .init_array
; CHECK-NEXT:    .p2align 3
; CHECK-NEXT:    data8.ua @fptr(ctor)

; CHECK-LABEL: table:
; CHECK-NEXT:    data8.ua .Llocal.desc
; CHECK-NEXT:    data8.ua @fptr(extern)
; CHECK-NEXT:    data8.ua @fptr(aliased)
; CHECK-NEXT:    data8.ua @fptr(global)

; CHECK:         .p2align 4
; CHECK-NEXT:  .Llocal.desc:
; CHECK-NEXT:    data16.ua @iplt(local)
; CHECK-NOT:   .desc:
