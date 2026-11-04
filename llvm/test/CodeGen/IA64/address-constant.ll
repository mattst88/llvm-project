; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=static < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=pic < %s | FileCheck %s

; A constant that holds an address is not read-only data, whether or not the
; module is position independent: the dynamic loader has to write the address
; of what another module defines, and that of a function's descriptor.

declare i32 @f(i32)
@gv = external global i32
@sv = internal global i32 1

define internal i32 @sf(i32 %x) {
  ret i32 %x
}

; CHECK:      .section .data.rel.ro,"aw",@progbits
; CHECK:      fns:
; CHECK-NEXT: data8.ua @fptr(f)
; CHECK-NEXT: data8.ua .Lsf.desc
@fns = constant [2 x ptr] [ptr @f, ptr @sf]

; CHECK:      vars:
; CHECK-NEXT: data8.ua gv
; CHECK-NEXT: data8.ua sv
@vars = constant [2 x ptr] [ptr @gv, ptr @sv]

; One that is short data is with the rest of the short data.
; CHECK:      .section .sdata,"aws",@progbits
; CHECK:      one:
; CHECK-NEXT: data8.ua sv
@one = internal constant ptr @sv

; CHECK:      .section .rodata
; CHECK:      nums:
@nums = constant [2 x i64] [i64 1, i64 2]

define ptr @use() {
  %p = load ptr, ptr @one
  ret ptr %p
}
