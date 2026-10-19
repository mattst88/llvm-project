; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A function goes from its arguments to its result by way of the calls it
; makes, and none of it by way of SelectionDAG.

declare i32 @g(i32, double, ptr)
declare signext i16 @h()

; The global pointer is kept in a register the calls leave alone, and each
; of them is followed by its return to r1.
define signext i32 @narrow(i32 signext %a, double %b, ptr %p, i8 zeroext %c) {
; CHECK-LABEL: narrow:
; CHECK:       alloc r36 = ar.pfs, 4, 2, 3, 0
; CHECK-NEXT:  mov r33 = r1
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  mov r38 = r32
; CHECK-NEXT:  mov r40 = r34
; CHECK-NEXT:  .save rp, r37
; CHECK-NEXT:  mov r37 = b0 ;;
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
; CHECK-NEXT:  mov r1 = r33
; CHECK-NEXT:  mov r32 = r8
; CHECK-NEXT:  br.call.sptk.many b0 = h ;;
; CHECK-NEXT:  zxt1 r14 = r35
; CHECK-NEXT:  sxt2 r15 = r8
; CHECK-NEXT:  mov r1 = r33 ;;
; CHECK-NEXT:  add [[S:r[0-9]+]] = r32, r14
; CHECK-NEXT:  mov b0 = r37 ;;
; CHECK-NEXT:  add r14 = [[S]], r15
; CHECK-NEXT:  mov ar.pfs = r36 ;;
; CHECK-NEXT:  sxt4 r8 = r14
; CHECK:       br.ret.sptk.many b0 ;;
  %r = call i32 @g(i32 %a, double %b, ptr %p)
  %s = call signext i16 @h()
  %t = sext i16 %s to i32
  %u = add i32 %r, %t
  %z = zext i8 %c to i32
  %v = add i32 %u, %z
  ret i32 %v
}

; The entry point is the first word of what the pointer is the address of,
; and the callee's global pointer the second.
define i64 @indirect(ptr %fp, i64 %a) {
; CHECK-LABEL: indirect:
; CHECK:       adds r15 = 8, r32
; CHECK-NEXT:  ld8 r14 = [r32]
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  mov r36 = r33 ;;
; CHECK-NEXT:  ld8 r15 = [r15]
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  mov b6 = r14 ;;
; CHECK-NEXT:  mov r1 = r15
; CHECK-NEXT:  br.call.sptk.many b0 = b6 ;;
; CHECK-NEXT:  mov b0 = r35
; CHECK-NEXT:  mov r1 = r32
  %r = call i64 %fp(i64 %a)
  ret i64 %r
}

; Extended precision that arrives in two slots is stored and loaded to be a
; floating-point value again.
define x86_fp80 @extended_slots([8 x float] %a, x86_fp80 %b) {
; CHECK-LABEL: extended_slots:
; CHECK:       st8 [r14] = r36
; CHECK-NEXT:  adds r15 = 8, r14 ;;
; CHECK-NEXT:  st8 [r15] = r37
; CHECK-NEXT:  ldfe f8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret x86_fp80 %b
}

; Two single-precision members that have no register are the halves of one
; slot.
define float @members_slot([10 x float] %a) {
; CHECK-LABEL: members_slot:
; CHECK:       shr.u r15 = r36, 32
; CHECK-NEXT:  setf.s f6 = r36 ;;
; CHECK-NEXT:  setf.s f7 = r15 ;;
; CHECK-NEXT:  fma.s f8 = f6, f1, f7
  %a8 = extractvalue [10 x float] %a, 8
  %a9 = extractvalue [10 x float] %a, 9
  %r = fadd float %a8, %a9
  ret float %r
}

declare void @take_members([10 x float])
declare void @take_any(i64, ...)

define void @call_members(float %a, float %b) {
; CHECK-LABEL: call_members:
; CHECK:       getf.s r14 = f9
; CHECK:       getf.s r15 = f8
; CHECK:       shl r14 = r14, 32
; CHECK-NEXT:  zxt4 r15 = r15 ;;
; CHECK-NEXT:  or r39 = r15, r14
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_members ;;
  %v0 = insertvalue [10 x float] zeroinitializer, float %a, 8
  %v1 = insertvalue [10 x float] %v0, float %b, 9
  call void @take_members([10 x float] %v1)
  ret void
}

; Extended precision the callee has not named is aligned to an even slot.
define void @call_any(x86_fp80 %a) {
; CHECK-LABEL: call_any:
; CHECK:       stfe [r14] = f8
; CHECK-NEXT:  ld8 r37 = [r14]
; CHECK-NEXT:  adds r14 = 8, r14 ;;
; CHECK-NEXT:  ld8 r38 = [r14]
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_any ;;
  call void (i64, ...) @take_any(i64 0, x86_fp80 %a)
  ret void
}

; An argument passed by value that arrived in registers is put in memory.
define i64 @by_value(ptr byval([16 x i8]) align 8 %p) {
; CHECK-LABEL: by_value:
; CHECK:       st8 [r15] = r32
; CHECK-NEXT:  st8 [r14] = r33
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %q = getelementptr i8, ptr %p, i64 8
  %v = load i64, ptr %q
  ret i64 %v
}

declare void @take_big(ptr byval([96 x i8]) align 8)

; The slots are read before the part in memory is copied, and are in
; registers that the copy leaves alone.
define void @call_big(ptr %p) {
; CHECK-LABEL: call_big:
; CHECK:       adds r12 = -32, r12
; CHECK:       mov r46 = 32
; CHECK-NEXT:  adds r45 = 64, r32 ;;
; CHECK:       adds r44 = 16, r12
; CHECK:       ld8 r33 = [r14]
; CHECK:       ld8 r40 = [r{{[0-9]+}}]
; CHECK-NEXT:  .save rp, r43
; CHECK-NEXT:  mov r43 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = memcpy ;;
; CHECK-NEXT:  .mmf
; CHECK-NEXT:  mov r44 = r33
; CHECK:       mov r51 = r40
; CHECK-NEXT:  mov r1 = r41
; CHECK-NEXT:  br.call.sptk.many b0 = take_big ;;
  call void @take_big(ptr byval([96 x i8]) align 8 %p)
  ret void
}
