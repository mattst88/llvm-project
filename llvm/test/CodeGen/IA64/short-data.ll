; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -data-sections < %s \
; RUN:   | FileCheck %s --check-prefix=SECTIONS
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -r -S - | FileCheck %s --check-prefix=OBJ

; A symbol that is this module's is at an offset from the global pointer and
; nothing is read to find it. The objects of eight bytes or fewer are kept
; together, close enough that the offset is one an add has room for, and a
; module trusts the others to have done the same with the ones they define.

@preemptible = global i64 5
@word = dso_local global i64 5
@zero = dso_local global i32 0
@bytes = dso_local global [8 x i8] c"abcdefg\00"
@nine = dso_local global [9 x i8] c"abcdefgh\00"
@constant = dso_local constant i64 7
@table = internal unnamed_addr constant [2 x i64] [i64 1, i64 2]
@private = internal unnamed_addr constant i64 9
@named = dso_local global i64 9, section ".mydata"
@common = common dso_local global i64 0
@elsewhere = external dso_local global i64
@hidden = external hidden global i64
@weak = extern_weak dso_local global i64

$grouped = comdat any
@grouped = linkonce_odr dso_local global i64 3, comdat

define ptr @preemptible_address() {
; CHECK-LABEL: preemptible_address:
; CHECK:       addl r14 = @ltoffx(preemptible), r1 ;;
; CHECK-NEXT:  ld8.mov r8 = [r14], preemptible
  ret ptr @preemptible
}

define ptr @word_address() {
; CHECK-LABEL: word_address:
; CHECK:       addl r8 = @gprel(word), r1
; CHECK-NEXT:  br.ret.sptk.many b0
  ret ptr @word
}

define i32 @zero_value() {
; CHECK-LABEL: zero_value:
; CHECK:       addl r14 = @gprel(zero), r1 ;;
; CHECK-NEXT:  ld4 r8 = [r14]
  %v = load i32, ptr @zero
  ret i32 %v
}

; An offset that is in the object goes with the symbol, and one that is not
; may be anywhere.
define ptr @bytes_inside() {
; CHECK-LABEL: bytes_inside:
; CHECK:       addl r8 = @gprel(bytes+7), r1
; CHECK-NEXT:  br.ret.sptk.many b0
  ret ptr getelementptr (i8, ptr @bytes, i64 7)
}

define ptr @bytes_outside() {
; CHECK-LABEL: bytes_outside:
; CHECK:       addl r15 = @gprel(bytes), r1
; CHECK-NEXT:  movl r14 = 4194304 ;;
; CHECK-NEXT:  add r8 = r15, r14
  ret ptr getelementptr (i8, ptr @bytes, i64 4194304)
}

define ptr @nine_address() {
; CHECK-LABEL: nine_address:
; CHECK:       movl r14 = @gprel(nine) ;;
; CHECK-NEXT:  add r8 = r14, r1
; CHECK-NEXT:  br.ret.sptk.many b0
  ret ptr @nine
}

define ptr @nine_offset() {
; CHECK-LABEL: nine_offset:
; CHECK:       movl r14 = @gprel(nine+4194304) ;;
; CHECK-NEXT:  add r8 = r14, r1
  ret ptr getelementptr (i8, ptr @nine, i64 4194304)
}

define ptr @constant_address() {
; CHECK-LABEL: constant_address:
; CHECK:       addl r8 = @gprel(constant), r1
  ret ptr @constant
}

define ptr @table_address() {
; CHECK-LABEL: table_address:
; CHECK:       movl r14 = @gprel(table) ;;
; CHECK-NEXT:  add r8 = r14, r1
  ret ptr @table
}

define ptr @private_address() {
; CHECK-LABEL: private_address:
; CHECK:       movl r14 = @gprel(private) ;;
  ret ptr @private
}

define ptr @named_address() {
; CHECK-LABEL: named_address:
; CHECK:       movl r14 = @gprel(named) ;;
  ret ptr @named
}

define ptr @common_address() {
; CHECK-LABEL: common_address:
; CHECK:       movl r14 = @gprel(common) ;;
  ret ptr @common
}

define ptr @elsewhere_address() {
; CHECK-LABEL: elsewhere_address:
; CHECK:       movl r14 = @gprel(elsewhere) ;;
  ret ptr @elsewhere
}

define ptr @hidden_address() {
; CHECK-LABEL: hidden_address:
; CHECK:       addl r8 = @gprel(hidden), r1
  ret ptr @hidden
}

define ptr @weak_address() {
; CHECK-LABEL: weak_address:
; CHECK:       addl r14 = @ltoffx(weak), r1 ;;
; CHECK-NEXT:  ld8.mov r8 = [r14], weak
  ret ptr @weak
}

; CHECK:      .section .sdata,"aws",@progbits
; CHECK:      preemptible:
; CHECK:      word:
; CHECK:      .section .sbss,"aws",@nobits
; CHECK:      zero:
; CHECK:      .section .sdata,"aws",@progbits
; CHECK:      bytes:
; CHECK:      .data
; CHECK:      nine:
; CHECK:      .section .sdata,"aws",@progbits
; CHECK:      constant:
; CHECK:      .section .rodata.cst16,"aM",@progbits,16
; CHECK:      table:
; CHECK:      .section .rodata.cst8,"aM",@progbits,8
; CHECK:      private:
; CHECK:      .section .mydata,"aw",@progbits
; CHECK:      named:
; CHECK:      .comm common,8,8
; CHECK:      .section .sdata.grouped,"awGs",@progbits,grouped,comdat
; CHECK:      grouped:

; SECTIONS:   .section .sdata.word,"aws",@progbits
; SECTIONS:   .section .sbss.zero,"aws",@nobits
; SECTIONS:   .section .data.nine,"aw",@progbits
; SECTIONS:   .section .sdata.constant,"aws",@progbits

; OBJ:        Name: .sdata
; OBJ-NEXT:   Type: SHT_PROGBITS
; OBJ-NEXT:   Flags [
; OBJ-NEXT:     SHF_ALLOC
; OBJ-NEXT:     SHF_IA_64_SHORT
; OBJ-NEXT:     SHF_WRITE
; OBJ:        Name: .sbss
; OBJ-NEXT:   Type: SHT_NOBITS
; OBJ-NEXT:   Flags [
; OBJ-NEXT:     SHF_ALLOC
; OBJ-NEXT:     SHF_IA_64_SHORT
; OBJ-NEXT:     SHF_WRITE

; OBJ:        R_IA64_LTOFF22X preemptible 0x0
; OBJ-NEXT:   R_IA64_LDXMOV preemptible 0x0
; OBJ-NEXT:   R_IA64_GPREL22 word 0x0
; OBJ-NEXT:   R_IA64_GPREL22 zero 0x0
; OBJ-NEXT:   R_IA64_GPREL22 bytes 0x7
; OBJ-NEXT:   R_IA64_GPREL22 bytes 0x0
; OBJ-NEXT:   R_IA64_GPREL64I nine 0x0
; OBJ-NEXT:   R_IA64_GPREL64I nine 0x400000
; OBJ-NEXT:   R_IA64_GPREL22 constant 0x0
; OBJ-NEXT:   R_IA64_GPREL64I .rodata.cst16 0x0
; OBJ-NEXT:   R_IA64_GPREL64I .rodata.cst8 0x0
; OBJ-NEXT:   R_IA64_GPREL64I named 0x0
; OBJ-NEXT:   R_IA64_GPREL64I common 0x0
; OBJ-NEXT:   R_IA64_GPREL64I elsewhere 0x0
; OBJ-NEXT:   R_IA64_GPREL22 hidden 0x0
; OBJ-NEXT:   R_IA64_LTOFF22X weak 0x0
; OBJ-NEXT:   R_IA64_LDXMOV weak 0x0
