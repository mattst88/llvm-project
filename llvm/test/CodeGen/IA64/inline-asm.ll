; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d -z - | FileCheck %s --check-prefix=OBJ

; What the source wrote in assembly is an instruction group of its own, with
; a stop before it and one after.

define i64 @alone(i64 %a, i64 %b) {
; CHECK-LABEL: alone:
; CHECK:       add r8 = r32, r33, 1 ;;
; CHECK-NEXT:  //APP
; CHECK-NEXT:  nop 5
; CHECK-NEXT:  ;;
; CHECK-NEXT:  //NO_APP
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
; OBJ-LABEL:   <alone>:
; OBJ:         nop.{{[im]}} 5
  %c = add i64 %a, %b
  call void asm sideeffect "nop 5", ""()
  %d = add i64 %c, 1
  ret i64 %d
}

define i64 @registers(i64 %a, i64 %b) {
; CHECK-LABEL: registers:
; CHECK:       add r14 = r32, r33
; CHECK-NEXT:  ;;
; CHECK-NEXT:  //NO_APP
; CHECK-NEXT:  adds r8 = 1, r14
  %r = call i64 asm "add $0 = $1, $2", "=r,r,r"(i64 %a, i64 %b)
  %s = add i64 %r, 1
  ret i64 %s
}

; Memory is a register with its address in it.

define i64 @memory(ptr %p) {
; CHECK-LABEL: memory:
; CHECK:       adds r14 = 8, r12 ;;
; CHECK-NEXT:  //APP
; CHECK-NEXT:  ld8 r15 = [r32]
; CHECK-NEXT:  ;;
; CHECK-NEXT:  st8 [r14] = r15
  %x = alloca i64
  %r = call i64 asm sideeffect "ld8 $0 = $1 ;; st8 $2 = $0", "=&r,*m,*S"(ptr elementtype(i64) %p, ptr elementtype(i64) %x)
  %v = load i64, ptr %x
  %s = add i64 %r, %v
  ret i64 %s
}

define i64 @immediate(i64 %a) {
; CHECK-LABEL: immediate:
; CHECK:       adds r8 = -100, r32
  %r = call i64 asm "adds $0 = $2, $1", "=r,r,I"(i64 %a, i64 -100)
  ret i64 %r
}

; An int that is negative is the number it is, and not thirty-two bits of
; an unsigned one.
define i64 @negative_int(i64 %a) {
; CHECK-LABEL: negative_int:
; CHECK:       adds r8 = -5, r32
  %r = call i64 asm "adds $0 = $2, $1", "=r,r,I"(i64 %a, i32 -5)
  ret i64 %r
}

define double @floating(double %a) {
; CHECK-LABEL: floating:
; CHECK:       fmerge.ns f8 = f8, f8
  %r = call double asm "fneg $0 = $1", "=f,f"(double %a)
  ret double %r
}

define i64 @named(i64 %a) {
; CHECK-LABEL: named:
; CHECK:       mov r15 = r32 ;;
; CHECK-NEXT:  //APP
; CHECK-NEXT:  mov r14 = r15
; CHECK:       //NO_APP
; CHECK-NEXT:  mov r8 = r14
  %r = call i64 asm "mov $0 = $1", "={r14},{r15},~{r16},~{f32},~{b6}"(i64 %a)
  ret i64 %r
}

define void @branch_register(i64 %a) {
; CHECK-LABEL: branch_register:
; CHECK:       mov b6 = r32 ;;
; CHECK-NEXT:  //APP
; CHECK-NEXT:  nop.b 0 // b6
  call void asm sideeffect "nop.b 0 // $0", "b"(i64 %a)
  ret void
}

; A register that is an output of this function has the name the frame gives
; it.

declare void @callee(i64)

define void @output() {
; CHECK-LABEL: output:
; CHECK:       alloc r33 = ar.pfs, 0, 3, 1, 0
; CHECK:       //APP
; CHECK-NEXT:  mov r35 = r0
; CHECK-NEXT:  ;;
; CHECK-NEXT:  //NO_APP
; CHECK-NEXT:  br.call.sptk.many b0 = callee ;;
  call void @callee(i64 1)
  %r = call i64 asm sideeffect "mov $0 = r0", "=r,~{r8},~{r9},~{r10},~{r11},~{r14},~{r15},~{r16},~{r17},~{r18},~{r19},~{r20},~{r21},~{r22},~{r23},~{r24},~{r25},~{r26},~{r27},~{r28},~{r29},~{r30},~{r31},~{r3}"()
  call void @callee(i64 %r)
  ret void
}
