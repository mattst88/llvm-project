; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A function says where its prologue ends, and of each instruction of it
; that saves the caller's frame state or the return link, or that makes the
; frame, that it does. An epilogue says where it gives the frame up.

declare i64 @ext(ptr)
declare void @stop() noreturn

define i64 @leaf(i64 %a) {
; CHECK-LABEL: leaf:
; CHECK-NEXT:  .prologue
; CHECK-NEXT:  .body
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .endp leaf
  %r = add i64 %a, 1
  ret i64 %r
}

define i64 @frame(i64 %a) {
; CHECK-LABEL: frame:
; CHECK-NEXT:  .prologue
; CHECK:       .fframe 304
; CHECK-NEXT:  adds r12 = -304, r12 ;;
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  adds r12 = 304, r12
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %p = alloca [40 x i64]
  %q = getelementptr i64, ptr %p, i64 %a
  store volatile i64 1, ptr %q
  %r = load volatile i64, ptr %p
  ret i64 %r
}

define i64 @call(i64 %a) {
; CHECK-LABEL: call:
; CHECK-NEXT:  .prologue
; CHECK:       .save ar.pfs, r34
; CHECK-NEXT:  alloc r34 = ar.pfs, 1, 3, 1, 0
; CHECK-NEXT:  .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12 ;;
; CHECK:       st8 [r36] = r32
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov ar.pfs = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %p = alloca i64
  store i64 %a, ptr %p
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @large(i64 %a) {
; The size is the frame's, whatever the instruction that makes it adds.
; CHECK-LABEL: large:
; CHECK:       mov r2 = -800000 ;;
; CHECK-NEXT:  .fframe 800000
; CHECK-NEXT:  add r12 = r2, r12
; CHECK:       .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       mov r2 = 800000 ;;
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  add r12 = r2, r12
  %p = alloca [100000 x i64]
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @dynamic(i64 %n) {
; A frame of a size that is not known is found from the copy of the stack
; pointer that the prologue makes.
; CHECK-LABEL: dynamic:
; CHECK:       .vframe r35
; CHECK-NEXT:  mov r35 = r12
; CHECK:       sub r12 = r12, r14 ;;
; CHECK-NEXT:  adds r36 = 16, r12
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  mov r12 = r35
  %p = alloca i64, i64 %n
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @aligned(i64 %n) {
; So is one that is aligned, and nothing is said of how far the stack
; pointer then moves.
; CHECK-LABEL: aligned:
; CHECK:       .vframe r35
; CHECK-NEXT:  mov r35 = r12
; CHECK-NEXT:  adds r12 = -112, r12 ;;
; CHECK-NEXT:  and r12 = -64, r12
; CHECK:       .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       .restore sp
; CHECK-NEXT:  mov r12 = r35
  %p = alloca i64, align 64
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

define i64 @returns(i64 %a) {
; The code after an epilogue has the frame that the code before it had.
; CHECK-LABEL: returns:
; CHECK:       .fframe 16
; CHECK-NEXT:  adds r12 = -16, r12
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK-NOT:   .body
; CHECK:       .label_state 1
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB6_5:
; CHECK-NEXT:  .body
; CHECK-NEXT:  .copy_state 1
; CHECK-NEXT:  br.call.sptk.many b0 = stop ;;
  %p = alloca i64
  %c = icmp eq i64 %a, 1
  br i1 %c, label %one, label %other
one:
  %r = call i64 @ext(ptr %p)
  ret i64 %r
other:
  %d = icmp eq i64 %a, 2
  br i1 %d, label %two, label %three
two:
  %s = call i64 @ext(ptr null)
  %t = add i64 %s, 2
  ret i64 %t
three:
  call void @stop()
  unreachable
}

; A register of the caller's that the function uses is saved in memory, and
; the unwinder is told which it is and not where it went: the general
; registers are lowest and the floating-point ones above them, each kind in
; the order of its numbers, and the last of them ends sixteen bytes above
; where the stack pointer was. The general registers leave their NaT bits in
; ar.unat, which is the caller's too.
define i64 @saves(i64 %a) {
; CHECK-LABEL: saves:
; CHECK:       .save ar.pfs, r33
; CHECK-NEXT:  alloc r33 = ar.pfs, 0, 4, 1, 0
; CHECK-NEXT:  .save ar.unat, r35
; CHECK-NEXT:  mov r35 = ar.unat
; CHECK-NEXT:  .fframe 64
; CHECK-NEXT:  adds r12 = -64, r12 ;;
; CHECK-NEXT:  adds r2 = 64, r12 ;;
; CHECK-NEXT:  .save.gf 0x0, 0x10
; CHECK-NEXT:  stf.spill [r2] = f16
; CHECK-NEXT:  adds r2 = 48, r12 ;;
; CHECK-NEXT:  .save.f 0x1
; CHECK-NEXT:  stf.spill [r2] = f2
; CHECK-NEXT:  adds r2 = 40, r12 ;;
; CHECK-NEXT:  .save.g 0x2
; CHECK-NEXT:  st8.spill [r2] = r5
; CHECK-NEXT:  adds r2 = 32, r12 ;;
; CHECK-NEXT:  .save.g 0x1
; CHECK-NEXT:  st8.spill [r2] = r4
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       br.call.sptk.many b0 = ext ;;
; CHECK-NEXT:  mov r1 = r32
; CHECK-NEXT:  adds r4 = 32, r12 ;;
; CHECK-NEXT:  ld8.fill r4 = [r4]
; CHECK-NEXT:  adds r5 = 40, r12 ;;
; CHECK-NEXT:  ld8.fill r5 = [r5]
; CHECK-NEXT:  adds r2 = 48, r12 ;;
; CHECK-NEXT:  ldf.fill f2 = [r2]
; CHECK-NEXT:  adds r2 = 64, r12 ;;
; CHECK-NEXT:  ldf.fill f16 = [r2]
; CHECK-NEXT:  mov ar.unat = r35
; CHECK-NEXT:  .restore sp
; CHECK-NEXT:  adds r12 = 64, r12
  %p = alloca i64
  call void asm sideeffect "", "~{r4},~{r5},~{f2},~{f16}"()
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

; One register fits in the sixteen bytes that the caller leaves at the stack
; pointer, and needs no frame.
define i64 @saves_one(i64 %a) {
; CHECK-LABEL: saves_one:
; CHECK:       .save ar.pfs, r33
; CHECK-NEXT:  alloc r33 = ar.pfs, 1, 2, 0, 0
; CHECK-NEXT:  .save ar.unat, r34
; CHECK-NEXT:  mov r34 = ar.unat
; CHECK-NEXT:  adds r2 = 8, r12 ;;
; CHECK-NEXT:  .save.g 0x4
; CHECK-NEXT:  st8.spill [r2] = r6
; CHECK-NEXT:  .body
; CHECK:       adds r6 = 8, r12 ;;
; CHECK-NEXT:  ld8.fill r6 = [r6]
; CHECK-NEXT:  mov ar.unat = r34
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  call void asm sideeffect "", "~{r6}"()
  ret i64 %a
}

; The registers are where the unwinder looks for them however the stack
; pointer has been aligned since, so they are found from where it was.
define i64 @saves_aligned(i64 %a) {
; CHECK-LABEL: saves_aligned:
; CHECK:       .vframe r36
; CHECK-NEXT:  mov r36 = r12
; CHECK-NEXT:  adds r12 = -112, r12 ;;
; CHECK-NEXT:  and r12 = -64, r12
; CHECK-NEXT:  mov r2 = r36 ;;
; CHECK-NEXT:  .save.gf 0x0, 0x80000
; CHECK-NEXT:  stf.spill [r2] = f31
; CHECK-NEXT:  adds r2 = -16, r36 ;;
; CHECK-NEXT:  .save.f 0x8
; CHECK-NEXT:  stf.spill [r2] = f5
; CHECK-NEXT:  adds r2 = -24, r36 ;;
; CHECK-NEXT:  .save.g 0x8
; CHECK-NEXT:  st8.spill [r2] = r7
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK:       adds r37 = 64, r12
; CHECK:       adds r7 = -24, r36 ;;
; CHECK-NEXT:  ld8.fill r7 = [r7]
  %p = alloca i64, align 64
  call void asm sideeffect "", "~{r7},~{f5},~{f31}"()
  %r = call i64 @ext(ptr %p)
  ret i64 %r
}

; A block that has no instructions is no region: the epilogue before it is
; the last thing in the function, and nothing is said of what follows.
define i64 @returns_before_nothing(i64 %a, i1 %c) {
; CHECK-LABEL: returns_before_nothing:
; CHECK:       .body
; CHECK-NOT:   .label_state
; CHECK:       .restore sp
; CHECK-NEXT:  adds r12 = 16, r12
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB{{[0-9_]+}}:
; CHECK-NEXT:  .endp returns_before_nothing
  %p = alloca i64
  %r = call i64 @ext(ptr %p)
  br i1 %c, label %done, label %never

done:
  ret i64 %r

never:
  unreachable
}
