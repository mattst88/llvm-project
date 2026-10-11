; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

; A pointer to a function points to the descriptor of the function, which
; is its entry point and its global pointer, so data that holds one asks the
; linker for the address of the descriptor.

declare void @extern()

define void @local() {
  ret void
}

@alias = alias void (), ptr @local

@object = global i64 0

; CHECK-LABEL: table:
; CHECK-NEXT:    data8.ua @fptr(extern)
; CHECK-NEXT:    data8.ua @fptr(local)
; CHECK-NEXT:    data8.ua @fptr(alias)
; CHECK-NEXT:    data8.ua object
@table = global [4 x ptr] [ptr @extern, ptr @local, ptr @alias, ptr @object]

; RELOC:      R_IA64_FPTR64LSB extern 0x0
; RELOC-NEXT: R_IA64_FPTR64LSB local 0x0
; RELOC-NEXT: R_IA64_FPTR64LSB alias 0x0
; RELOC-NEXT: R_IA64_DIR64LSB object 0x0
