; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=OBJ

;; llvm.reloc.none is a reference that keeps a section and changes no code.
;; It is written with the generic name of the relocation, which the object
;; writer has to know.

; CHECK-LABEL: f:
; CHECK:       adds r8 = 1, r32
; CHECK-NEXT:  .Lreloc_none0:
; CHECK-NEXT:  .reloc .Lreloc_none0, BFD_RELOC_NONE, sym
; CHECK-NEXT:  br.ret.sptk.many b0

; OBJ:      .rela.text {
; OBJ-NEXT:   0x10 R_IA64_NONE sym 0x0
; OBJ-NEXT: }

define i64 @f(i64 %a) {
  %b = add i64 %a, 1
  call void @llvm.reloc.none(metadata !"sym")
  ret i64 %b
}

declare void @llvm.reloc.none(metadata)
