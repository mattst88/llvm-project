; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK,PIC
; RUN: llc -mtriple=ia64-unknown-linux-gnu -relocation-model=static \
; RUN:   -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,EXEC
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-readobj -r --symbols - | FileCheck %s --check-prefix=OBJ

; What a thread has of its own is found from the thread pointer, r13, by an
; offset that the link editor knows for a symbol the program defines and that
; the loader leaves in the linkage table for one the program only uses. A
; module that may be loaded later asks __tls_get_addr, with the module and
; the offset in it, or with the module alone for the symbols it defines
; itself, whose offsets the link editor knows.

@le = thread_local(localexec) global i64 3
@ie = external thread_local(initialexec) global i64
@ld = internal thread_local(localdynamic) global [4 x i64] zeroinitializer
@gd = external thread_local global i64

; The add of an offset has two bits for its register, so it is not r13.
define i64 @local_exec() {
; CHECK-LABEL: local_exec:
; CHECK:       mov r3 = r13 ;;
; CHECK-NEXT:  addl r14 = @tprel(le), r3 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0
  %p = call ptr @llvm.threadlocal.address.p0(ptr @le)
  %v = load i64, ptr %p
  ret i64 %v
}

define i64 @initial_exec() {
; CHECK-LABEL: initial_exec:
; CHECK:       addl r14 = @ltoff(@tprel(ie)), r1 ;;
; CHECK-NEXT:  ld8 r14 = [r14] ;;
; CHECK-NEXT:  add r14 = r14, r13 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0
  %p = call ptr @llvm.threadlocal.address.p0(ptr @ie)
  %v = load i64, ptr %p
  ret i64 %v
}

; A program is not loaded later, and what it defines is at an offset from
; the thread pointer whatever the source asked for.
define i64 @local_dynamic() {
; CHECK-LABEL: local_dynamic:
; PIC:         addl r14 = @ltoff(@dtpmod(ld)), r1
; PIC-NEXT:    mov r36 = r0
; PIC-NEXT:    mov r32 = r1 ;;
; PIC-NEXT:    ld8 r35 = [r14]
; PIC-NEXT:    br.call.sptk.many b0 = __tls_get_addr ;;
; PIC-NEXT:    mov r3 = r8
; PIC-NEXT:    mov r1 = r32 ;;
; PIC-NEXT:    addl r14 = @dtprel(ld), r3 ;;
; PIC-NEXT:    adds r14 = 16, r14 ;;
; PIC-NEXT:    ld8 r8 = [r14]
; EXEC:        mov r3 = r13 ;;
; EXEC-NEXT:   addl r14 = @tprel(ld), r3 ;;
; EXEC-NEXT:   adds r14 = 16, r14 ;;
; EXEC-NEXT:   ld8 r8 = [r14]
; EXEC-NEXT:   br.ret.sptk.many b0
  %p = call ptr @llvm.threadlocal.address.p0(ptr @ld)
  %e = getelementptr i64, ptr %p, i64 2
  %v = load i64, ptr %e
  ret i64 %v
}

define ptr @general_dynamic() {
; CHECK-LABEL: general_dynamic:
; PIC:         addl r14 = @ltoff(@dtpmod(gd)), r1
; PIC-NEXT:    mov r32 = r1 ;;
; PIC-NEXT:    ld8 r35 = [r14]
; PIC-NEXT:    addl r14 = @ltoff(@dtprel(gd)), r1 ;;
; PIC-NEXT:    ld8 r36 = [r14]
; PIC-NEXT:    br.call.sptk.many b0 = __tls_get_addr ;;
; PIC-NEXT:    mov r1 = r32
; EXEC:        addl r14 = @ltoff(@tprel(gd)), r1 ;;
; EXEC-NEXT:   ld8 r14 = [r14] ;;
; EXEC-NEXT:   add r8 = r14, r13
; EXEC-NEXT:   br.ret.sptk.many b0
  %p = call ptr @llvm.threadlocal.address.p0(ptr @gd)
  ret ptr %p
}

declare ptr @llvm.threadlocal.address.p0(ptr)

; OBJ:      R_IA64_TPREL22 le 0x0
; OBJ-NEXT: R_IA64_LTOFF_TPREL22 ie 0x0
; OBJ-NEXT: R_IA64_LTOFF_DTPMOD22 ld 0x0
; OBJ-NEXT: R_IA64_PCREL21B __tls_get_addr 0x0
; OBJ-NEXT: R_IA64_DTPREL22 ld 0x0
; OBJ-NEXT: R_IA64_LTOFF_DTPMOD22 gd 0x0
; OBJ-NEXT: R_IA64_LTOFF_DTPREL22 gd 0x0
; OBJ-NEXT: R_IA64_PCREL21B __tls_get_addr 0x0

; OBJ:      Name: ld
; OBJ:      Type: TLS
; OBJ:      Name: le
; OBJ:      Type: TLS
; OBJ:      Name: ie
; OBJ:      Type: TLS
; OBJ:      Name: gd
; OBJ:      Type: TLS
