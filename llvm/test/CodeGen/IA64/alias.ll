; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readelf -s - | FileCheck %s --check-prefix=SYM

; An alias of a function is another name for its code. A pointer to the alias
; is the address of a descriptor like a pointer to any other function.

@other = alias void (), ptr @function
@object = global i64 1
@same = alias i64, ptr @object
@pointer = global ptr @other

define void @function() {
  ret void
}

; CHECK-LABEL: pointer:
; CHECK:       data8.ua @fptr(other)

; CHECK:       .globl other
; CHECK-NEXT:  .type other,@function
; CHECK-NEXT:  other = function
; CHECK:       .globl same
; CHECK-NEXT:  same = object

; SYM-DAG: {{0+}} 16 FUNC   GLOBAL DEFAULT [[#TEXT:]] function
; SYM-DAG: {{0+}} 16 FUNC   GLOBAL DEFAULT [[#TEXT]] other
