; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s | llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

; A variable of the small model is where an add of twenty-two bits reaches
; from nothing, so neither the global pointer nor the linkage table is asked,
; whose module it is in or how large it is.

@sm = global i64 0, code_model "small"
@esm = external global i64, code_model "small"
@ssm = internal global i64 0, code_model "small"
@big = global [4096 x i8] zeroinitializer, code_model "small"
@other = external global i64

define i64 @rd() {
; CHECK-LABEL: rd:
; CHECK-DAG:   mov {{r[0-9]+}} = sm{{ |$}}
; CHECK-DAG:   mov {{r[0-9]+}} = esm{{ |$}}
; CHECK-DAG:   mov {{r[0-9]+}} = ssm{{ |$}}
; CHECK:       br.ret.sptk.many b0
  %a = load i64, ptr @sm
  %b = load i64, ptr @esm
  %c = load i64, ptr @ssm
  %d = add i64 %a, %b
  %e = add i64 %d, %c
  ret i64 %e
}

define ptr @ad() {
; CHECK-LABEL: ad:
; CHECK:       mov r8 = esm{{ |$}}
  ret ptr @esm
}

; An offset into the object goes with the symbol.
define void @wr(i8 %v) {
; CHECK-LABEL: wr:
; CHECK:       mov [[A:r[0-9]+]] = big+100
; CHECK:       st1 {{\[}}[[A]]] = r32
  store i8 %v, ptr getelementptr (i8, ptr @big, i64 100)
  ret void
}

; One past it does not.
define ptr @past() {
; CHECK-LABEL: past:
; CHECK:       mov [[A:r[0-9]+]] = esm
; CHECK:       adds r8 = 4096, [[A]]
  ret ptr getelementptr (i8, ptr @esm, i64 4096)
}

; Any other is found as before.
define ptr @ot() {
; CHECK-LABEL: ot:
; CHECK:       addl {{r[0-9]+}} = @ltoffx(other), r1
  ret ptr @other
}

; RELOC:      R_IA64_IMM22 sm 0x0
; RELOC:      R_IA64_IMM22 esm 0x0
; RELOC:      R_IA64_IMM22 .sbss 0x8
; RELOC:      R_IA64_IMM22 big 0x64
; RELOC:      R_IA64_LTOFF22X other 0x0
; RELOC:      R_IA64_LDXMOV other 0x0
