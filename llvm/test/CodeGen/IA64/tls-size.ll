; RUN: llc -mtriple=ia64-unknown-linux-gnu -tls-size=14 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=SIZE14
; RUN: llc -mtriple=ia64-unknown-linux-gnu -tls-size=22 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=SIZE22
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=SIZE22
; RUN: llc -mtriple=ia64-unknown-linux-gnu -tls-size=64 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefix=SIZE64
; RUN: llc -mtriple=ia64-unknown-linux-gnu -tls-size=14 -filetype=obj < %s \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=OBJ14
; RUN: llc -mtriple=ia64-unknown-linux-gnu -tls-size=64 -filetype=obj < %s \
; RUN:   | llvm-readobj -r - | FileCheck %s --check-prefix=OBJ64

; The offset of a symbol from the start of what a thread has is known to the
; link editor when the module defines the symbol. How it is added depends on
; how large it may be: one of fourteen bits is added to any register, one of
; twenty-two bits to one of the first four, and one of any size is loaded,
; as a constant if the module is the program and from the linkage table if
; it is not.

@le = thread_local(localexec) global i64 3
@ld = internal thread_local(localdynamic) global [4 x i64] zeroinitializer

define i64 @local_exec() {
; SIZE14-LABEL: local_exec:
; SIZE14:       adds r14 = @tprel(le), r13 ;;
; SIZE14-NEXT:  ld8 r8 = [r14]
;
; SIZE22-LABEL: local_exec:
; SIZE22:       mov r3 = r13 ;;
; SIZE22-NEXT:  addl r14 = @tprel(le), r3 ;;
; SIZE22-NEXT:  ld8 r8 = [r14]
;
; SIZE64-LABEL: local_exec:
; SIZE64:       movl r14 = @tprel(le) ;;
; SIZE64-NEXT:  add r14 = r14, r13 ;;
; SIZE64-NEXT:  ld8 r8 = [r14]
  %v = load i64, ptr @le
  ret i64 %v
}

define ptr @local_dynamic() {
; SIZE14-LABEL: local_dynamic:
; SIZE14:       addl r14 = @ltoff(@dtpmod(ld)), r1
; SIZE14:       br.call.sptk.many b0 = __tls_get_addr ;;
; SIZE14-NEXT:  .mmi
; SIZE14-NEXT:  mov r1 = r32
; SIZE14-NEXT:  adds r8 = @dtprel(ld), r8
;
; SIZE22-LABEL: local_dynamic:
; SIZE22:       br.call.sptk.many b0 = __tls_get_addr ;;
; SIZE22-NEXT:  mov r3 = r8
; SIZE22-NEXT:  mov r1 = r32 ;;
; SIZE22-NEXT:  mov b0 = r34
; SIZE22-NEXT:  addl r8 = @dtprel(ld), r3
;
; SIZE64-LABEL: local_dynamic:
; SIZE64:       br.call.sptk.many b0 = __tls_get_addr ;;
; SIZE64-NEXT:  mov r1 = r32 ;;
; SIZE64-NEXT:  addl r14 = @ltoff(@dtprel(ld)), r1 ;;
; SIZE64-NEXT:  ld8 r14 = [r14] ;;
; SIZE64-NEXT:  add r8 = r14, r8
  ret ptr @ld
}

; OBJ14: R_IA64_TPREL14 le 0x0
; OBJ14: R_IA64_LTOFF_DTPMOD22 ld 0x0
; OBJ14: R_IA64_DTPREL14 ld 0x0

; OBJ64: R_IA64_TPREL64I le 0x0
; OBJ64: R_IA64_LTOFF_DTPMOD22 ld 0x0
; OBJ64: R_IA64_LTOFF_DTPREL22 ld 0x0
