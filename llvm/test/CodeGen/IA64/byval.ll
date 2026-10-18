; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; An argument that is passed by value is its bytes in the slots that they
; fill, the registers r32 to r39 and then memory, as an aggregate is under
; the conventions.

%struct.s = type { i64, i64 }
%struct.odd = type <{ i64, i16, i8 }>
%struct.big = type { [128 x i64] }
%struct.al = type { i128, i64 }

declare void @take(ptr byval(%struct.s) align 8)
declare void @take_odd(ptr byval(%struct.odd) align 8)
declare void @take_big(ptr byval(%struct.big) align 8, i64)
declare void @take_al(i64, ptr byval(%struct.al) align 16)
declare void @take_split(i64, i64, i64, i64, i64, i64,
                         ptr byval(%struct.al) align 16)

define void @small(ptr %p) {
; CHECK-LABEL: small:
; CHECK:       ld8 r36 = [r32], 8
; CHECK-NEXT:  mov r33 = r1 ;;
; CHECK-NEXT:  ld8 r37 = [r32]
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  mov r35 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take ;;
  call void @take(ptr byval(%struct.s) align 8 %p)
  ret void
}

; The bytes after the last of the argument are not read.
define void @odd(ptr %p) {
; CHECK-LABEL: odd:
; CHECK:       adds r14 = 8, r32
; CHECK-NEXT:  ld8 r35 = [r32], 10 ;;
; CHECK-NEXT:  ld1 r15 = [r32]
; CHECK-NEXT:  mov r32 = r1
; CHECK-NEXT:  ld2 r14 = [r14] ;;
; CHECK-NEXT:  shl r15 = r15, 16 ;;
; CHECK-NEXT:  or r36 = r14, r15
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_odd ;;
  call void @take_odd(ptr byval(%struct.odd) align 8 %p)
  ret void
}

; What the registers have no room for is copied to where the callee looks
; for it, by the library if it is large, and before the other arguments are
; put where they go.
define void @large(ptr %p, i64 %x) {
; CHECK-LABEL: large:
; CHECK:       adds r12 = -976, r12
; CHECK:       mov r45 = r32
; CHECK-NEXT:  mov r41 = r1 ;;
; CHECK-NEXT:  adds r44 = 16, r12
; CHECK-NEXT:  mov r46 = 960
; CHECK:       ld8 r{{[0-9]+}} = [r45], 64
; CHECK-NEXT:  .save rp, r43
; CHECK-NEXT:  mov r43 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = memcpy ;;
; CHECK-NEXT:  .mmi
; CHECK-NEXT:  adds r14 = 976, r12
; CHECK:       st8 [r14] = r33
; CHECK-NEXT:  br.call.sptk.many b0 = take_big ;;
  call void @take_big(ptr byval(%struct.big) align 8 %p, i64 %x)
  ret void
}

; One that is aligned to sixteen bytes begins at an even slot.
define void @aligned(ptr %p) {
; CHECK-LABEL: aligned:
; CHECK:       mov r35 = r0 ;;
; CHECK-NEXT:  ld8 r37 = [r14], 24 ;;
; CHECK-NEXT:  ld8 r40 = [r14]
; CHECK:       ld8 r39 = [r15] ;;
; CHECK-NEXT:  ld8 r38 = [r14]
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_al ;;
  call void @take_al(i64 0, ptr byval(%struct.al) align 16 %p)
  ret void
}

; It may be partly in registers and partly in memory.
define void @split(ptr %p) {
; CHECK-LABEL: split:
; CHECK:       ld8 r42 = [r14]
; CHECK-NEXT:  adds r15 = 16, r12
; CHECK:       ld8 r41 = [r32], 24 ;;
; CHECK-NEXT:  st8 [r15] = r14
; CHECK-NEXT:  adds r15 = 24, r12
; CHECK-NEXT:  ld8 r14 = [r32]
; CHECK:       st8 [r15] = r14
; CHECK-NEXT:  .save rp, r34
; CHECK-NEXT:  mov r34 = b0
; CHECK-NEXT:  .body
; CHECK-NEXT:  br.call.sptk.many b0 = take_split ;;
  call void @take_split(i64 0, i64 1, i64 2, i64 3, i64 4, i64 5,
                        ptr byval(%struct.al) align 16 %p)
  ret void
}

; The callee is given an address, so it writes what came in registers to
; memory. The scratch area has room for two slots.
define i64 @callee(ptr byval(%struct.s) align 8 %p) {
; CHECK-LABEL: callee:
; CHECK:       mov r14 = r12
; CHECK-NEXT:  mov r8 = r32 ;;
; CHECK-NEXT:  adds r15 = 8, r14 ;;
; CHECK-NEXT:  st8 [r15] = r33
; CHECK-NEXT:  st8 [r14] = r0
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i64, ptr %p
  store i64 0, ptr %p
  ret i64 %v
}

; What came in registers is written below what came in memory, so that the
; argument is in one piece.
define i64 @callee_split(i64, i64, i64, i64, i64, i64,
                         ptr byval(%struct.al) align 16 %p) {
; CHECK-LABEL: callee_split:
; CHECK:       mov r14 = r12 ;;
; CHECK-NEXT:  or r15 = 8, r14
; CHECK-NEXT:  st8 [r14] = r38
; CHECK-NEXT:  adds r14 = 16, r14 ;;
; CHECK-NEXT:  st8 [r15] = r39
; CHECK-NEXT:  ld8 r14 = [r14] ;;
; CHECK-NEXT:  add r8 = r14, r38
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %q = getelementptr i64, ptr %p, i64 2
  %v = load i64, ptr %q
  %w = load i64, ptr %p
  %r = add i64 %v, %w
  ret i64 %r
}

define ptr @callee_big(ptr byval(%struct.big) align 8 %p, i64 %x) {
; CHECK-LABEL: callee_big:
; CHECK:       adds r12 = -48, r12 ;;
; CHECK:       mov r8 = r12 ;;
; CHECK-NEXT:  adds r14 = 56, r8
; CHECK-NEXT:  or r15 = 8, r8
; CHECK-NEXT:  st8 [r8] = r32 ;;
; CHECK-NEXT:  st8 [r14] = r39
; CHECK:       adds r12 = 48, r12
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  ret ptr %p
}

; One that is all in memory is used where it is.
define i64 @callee_mem(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                       ptr byval(%struct.al) align 16 %p) {
; CHECK-LABEL: callee_mem:
; CHECK:       adds r14 = 32, r12 ;;
; CHECK-NEXT:  adds r14 = 16, r14 ;;
; CHECK-NEXT:  ld8 r8 = [r14]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %q = getelementptr i64, ptr %p, i64 2
  %v = load i64, ptr %q
  ret i64 %v
}

; The function reads it through its address, so the slots that came in
; registers are written before any of it is read.
define i64 @callee_read(i64 %a, i64, i64, i64, i64, i64,
                        ptr byval([24 x i8]) align 8 %s) {
; CHECK-LABEL: callee_read:
; CHECK-DAG:   st8 [r14] = r38
; CHECK-DAG:   st8 [r15] = r39
; CHECK:       ld1
; CHECK:       ld1
; CHECK:       br.ret.sptk.many b0 ;;
  %p = getelementptr i8, ptr %s, i64 9
  %v = load i8, ptr %p
  %z = zext i8 %v to i64
  %m = mul i64 %z, %a
  %q = getelementptr i8, ptr %s, i64 3
  %w = load i8, ptr %q
  %y = zext i8 %w to i64
  %n = mul i64 %y, %m
  ret i64 %n
}
