; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -S - | FileCheck %s --check-prefix=OBJ
; RUN: sed 's/i32 8}/i32 0}/' %s | llc -mtriple=ia64-unknown-linux-gnu \
; RUN:   | FileCheck %s --check-prefix=NONE

; An object that the source puts in a section of short data is short data
; whatever size it has, as it is for GCC: one add to the global pointer finds
; it. The section is the one the short data of the module is in and has the
; flags of that one, also for an object that is never written. A section
; named by a pragma is not known to be one of short data, and an object in it
; is found the long way however small it is.

@named = dso_local global i32 1, section ".sdata"
@zero = dso_local global i32 0, section ".sbss"
@big = dso_local global [100 x i8] c"\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", section ".sdata.big"
@constant = dso_local constant i32 5, section ".sdata"
@small = dso_local global i32 3
@other = dso_local global i32 7, section ".mydata"
@pragma = dso_local global i32 4 #0

define ptr @named_address() {
; CHECK-LABEL: named_address:
; CHECK:       addl r8 = @gprel(named), r1
; NONE-LABEL:  named_address:
; NONE:        movl r14 = @gprel(named)
  ret ptr @named
}

define ptr @zero_address() {
; CHECK-LABEL: zero_address:
; CHECK:       addl r8 = @gprel(zero), r1
  ret ptr @zero
}

define ptr @big_address() {
; CHECK-LABEL: big_address:
; CHECK:       addl r8 = @gprel(big), r1
  ret ptr @big
}

define ptr @constant_address() {
; CHECK-LABEL: constant_address:
; CHECK:       addl r8 = @gprel(constant), r1
  ret ptr @constant
}

define ptr @other_address() {
; CHECK-LABEL: other_address:
; CHECK:       movl r14 = @gprel(other)
  ret ptr @other
}

define ptr @pragma_address() {
; CHECK-LABEL: pragma_address:
; CHECK:       movl r14 = @gprel(pragma)
  ret ptr @pragma
}

; CHECK:     .section .sdata,"aws",@progbits
; CHECK:     named:
; CHECK:     .section .sbss,"aws",@nobits
; CHECK:     zero:
; CHECK:     .section .sdata.big,"aws",@progbits
; CHECK:     big:
; CHECK:     .section .sdata,"aws",@progbits
; CHECK:     constant:
; CHECK-NOT: .section
; CHECK:     small:
; CHECK:     .section .mydata,"aw",@progbits
; CHECK:     .section mine,"aw",@progbits
; CHECK-NOT: unique

; There is one .sdata, and it is short.
; OBJ:      Name: .sdata (
; OBJ:      Flags [
; OBJ-NEXT:   SHF_ALLOC
; OBJ-NEXT:   SHF_IA_64_SHORT
; OBJ-NEXT:   SHF_WRITE
; OBJ-NOT:  Name: .sdata (

attributes #0 = { "data-section"="mine" }

!llvm.module.flags = !{!0}
!0 = !{i32 8, !"SmallDataLimit", i32 8}
