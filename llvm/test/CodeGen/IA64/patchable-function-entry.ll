; RUN: split-file %s %t
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   < %t/entry.ll | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %t/entry.ll \
; RUN:   | llvm-objdump -dr --no-show-raw-insn - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %t/entry.ll \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=REL
; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %t/xray.ll -o /dev/null \
; RUN:   2>&1 | FileCheck %s --check-prefix=XRAY

; The nops that a patchable function entry asks for are counted as gcc counts
; them, by the instruction, so three of them are a bundle. Those after the
; entry are the first instructions of the function and end with a stop, since
; an alloc that follows has to begin an instruction group. Those before the
; entry are made up to whole bundles, because the entry begins one. What the
; section records is where the first of them is. XRay, which wants more than
; nops at the same place, is refused by the pass that would instrument for it.

;--- entry.ll
; CHECK-LABEL: after:
; CHECK-NEXT: .Lfunc_begin0:
; CHECK-NEXT: .prologue
; CHECK-NEXT: .body
; CHECK: nop 0
; CHECK-NEXT: nop 0 ;;
; CHECK-NEXT: adds r8 = 1, r32
; CHECK: .section __patchable_function_entries,"awo",@progbits,after
; CHECK-NEXT: .p2align 3, 0x0
; CHECK-NEXT: data8.ua .Lfunc_begin0

; OBJ-LABEL: <after>:
; OBJ-NEXT: 0: nop.m 0 // [MII]
; OBJ-NEXT: 6: nop.i 0 ;;
; OBJ-NEXT: c: adds r8 = 1, r32
define i32 @after(i32 %x) "patchable-function-entry"="2" {
  %y = add i32 %x, 1
  ret i32 %y
}

; CHECK: .type both,@function
; CHECK-NEXT: [[BEFORE:\.Ltmp[0-9]+]]:
; CHECK-NEXT: nop 0
; CHECK-NEXT: .proc both
; CHECK-NEXT: both:
; CHECK: .prologue
; CHECK: nop 0
; CHECK-NEXT: nop 0
; CHECK-NEXT: nop 0 ;;
; CHECK-NEXT: .save ar.pfs, r34
; CHECK-NEXT: alloc r34 = ar.pfs, 1, 3, 1, 0
; CHECK: .section __patchable_function_entries,"awo",@progbits,both
; CHECK: data8.ua [[BEFORE]]

; OBJ: 40: nop.m 0 // [MII]
; OBJ-NEXT: 46: nop.i 0
; OBJ-NEXT: 4c: nop.i 0
; OBJ-LABEL: <both>:
; OBJ-NEXT: 50: nop.m 0 // [MII]
; OBJ-NEXT: 56: nop.i 0
; OBJ-NEXT: 5c: nop.i 0 ;;
; OBJ-NEXT: 60: alloc r34 = ar.pfs,
define i32 @both(i32 %x) "patchable-function-entry"="3" "patchable-function-prefix"="1" {
  %y = call i32 @ext(i32 %x)
  ret i32 %y
}

; CHECK: .type before,@function
; CHECK-NEXT: [[FOUR:\.Ltmp[0-9]+]]:
; CHECK-NEXT: nop 0
; CHECK-NEXT: nop 0
; CHECK-NEXT: nop 0
; CHECK-NEXT: nop 0
; CHECK-NEXT: .proc before
; CHECK-NEXT: before:
; CHECK: .body
; CHECK-NOT: nop
; CHECK: adds r8 = 2, r32
; CHECK: data8.ua [[FOUR]]
define i32 @before(i32 %x) "patchable-function-entry"="0" "patchable-function-prefix"="4" {
  %y = add i32 %x, 2
  ret i32 %y
}

declare i32 @ext(i32)

; REL: R_IA64_DIR64LSB .text 0x0
; REL: R_IA64_DIR64LSB .text 0x40

;--- xray.ll
; XRAY: An attempt to perform XRay instrumentation for an unsupported target.
define i32 @f(i32 %x) "function-instrument"="xray-always" {
  %y = add i32 %x, 1
  ret i32 %y
}
