; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj -o %t.o < %s
; RUN: llvm-readelf -S %t.o | FileCheck %s --check-prefix=ELF

; An object that the source puts in a section of short data and that is to
; be kept, or that goes with another, has a section of its own with that
; name. The address of it is still taken as a distance from the global
; pointer, so the section has to be marked short as the other one is, or
; the link editor puts it where that distance does not reach.

@kept = dso_local global i32 1, section ".sdata", !retain !0
@constant = dso_local constant i32 2, section ".sdata", !retain !0
@zero = dso_local global i32 0, section ".sbss", !retain !0
@anchor = dso_local global i32 3
@with = dso_local global i32 4, section ".sdata.with", !associated !1
@plain = dso_local global i32 5, section ".sdata"
@elsewhere = dso_local global i32 6, section "other", !retain !0

@llvm.used = appending global [4 x ptr] [ptr @kept, ptr @constant, ptr @zero, ptr @elsewhere], section "llvm.metadata"

define dso_local i32 @read() {
; CHECK-LABEL: read:
; CHECK:       addl r{{[0-9]+}} = @gprel(kept), r1
; CHECK:       addl r{{[0-9]+}} = @gprel(with), r1
entry:
  %a = load i32, ptr @kept
  %b = load i32, ptr @with
  %c = add i32 %a, %b
  ret i32 %c
}

; CHECK:     .section .sdata,"awRs",@progbits,unique,1
; CHECK:     kept:
; CHECK:     .section .sdata,"awRs",@progbits,unique,2
; CHECK:     constant:
; CHECK:     .section .sbss,"awRs",@nobits,unique,3
; CHECK:     zero:
; CHECK:     .section .sdata,"aws",@progbits
; CHECK:     anchor:
; CHECK:     .section .sdata.with,"awos",@progbits,anchor,unique,4
; CHECK:     with:
; CHECK:     .section .sdata,"aws",@progbits
; CHECK:     plain:
; CHECK:     .section other,"awR",@progbits,unique,5
; CHECK:     elsewhere:

; ELF:      .sdata      PROGBITS {{.*}} WARp
; ELF-NEXT: .sdata      PROGBITS {{.*}} WARp
; ELF-NEXT: .sbss       NOBITS   {{.*}} WARp
; ELF-NEXT: .sdata      PROGBITS {{.*}} WAp
; ELF-NEXT: .sdata.with PROGBITS {{.*}} WALp
; ELF-NEXT: other       PROGBITS {{.*}} WAR{{ }}

!0 = !{}
!1 = !{ptr @anchor}
