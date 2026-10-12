; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Eighty bits in one register are an extended value, whatever the program
; takes them for. They become the two registers of an integer by being stored
; as ten bytes and loaded as eight and two, and the other way about.

; CHECK-LABEL: sb:
; CHECK:      mov r14 = r12
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: adds r15 = 8, r14
; CHECK-NEXT: stfe [r14] = f8 ;;
; CHECK-NEXT: ld2 r15 = [r15]
; CHECK-NEXT: ld8 r14 = [r14] ;;
; CHECK-NEXT: sxt2 r15 = r15
; CHECK-NEXT: cmp.ltu p6, p7 = r14, r0
; CHECK-NEXT: mov r14 = r0 ;;
; CHECK-NEXT: (p6) mov r14 = 1
; CHECK-NEXT: cmp.lt p7, p6 = -1, r15 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: cmp.eq p6, p7 = 0, r15 ;;
; CHECK-NEXT: (p6) mov r8 = r14
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @sb(x86_fp80 %x) {
  %b = bitcast x86_fp80 %x to i80
  %c = icmp slt i80 %b, 0
  ret i1 %c
}

; CHECK-LABEL: mk:
; CHECK:      zxt2 r14 = r33
; CHECK-NEXT: mov r15 = r12
; CHECK-NEXT: or r16 = 0, r32 ;;
; CHECK-NEXT: or r14 = r0, r14
; CHECK-NEXT: adds r17 = 8, r15
; CHECK-NEXT: st8 [r15] = r16 ;;
; CHECK-NEXT: st2 [r17] = r14
; CHECK-NEXT: ldfe f8 = [r15]
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define x86_fp80 @mk(i64 %lo, i16 %hi) {
  %a = zext i64 %lo to i80
  %b = zext i16 %hi to i80
  %c = shl i80 %b, 64
  %d = or i80 %a, %c
  %e = bitcast i80 %d to x86_fp80
  ret x86_fp80 %e
}

; CHECK-LABEL: bits:
; CHECK:      mov r14 = r12 ;;
; CHECK-NEXT: stfe [r14] = f8
; CHECK-NEXT: adds r15 = 8, r14
; CHECK-NEXT: ld8 r14 = [r14] ;;
; CHECK-NEXT: ld2 r15 = [r15]
; CHECK-NEXT: xor r8 = -1, r14 ;;
; CHECK-NEXT: xor r9 = -1, r15
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i80 @bits(x86_fp80 %x) {
  %b = bitcast x86_fp80 %x to i80
  %c = xor i80 %b, -1
  ret i80 %c
}

; CHECK-LABEL: isnan:
; CHECK:      mov r14 = r12
; CHECK-NEXT: mov r16 = 32767
; CHECK-NEXT: movl r17 = -9223372036854775808
; CHECK-NEXT: mov r8 = r0 ;;
; CHECK-NEXT: stfe [r14] = f8
; CHECK-NEXT: adds r15 = 8, r14
; CHECK-NEXT: ld8 r14 = [r14] ;;
; CHECK-NEXT: ld2 r15 = [r15]
; CHECK-NEXT: and r14 = -1, r14 ;;
; CHECK-NEXT: and r15 = r15, r16
; CHECK-NEXT: cmp.ltu p6, p7 = r17, r14
; CHECK-NEXT: mov r14 = r0 ;;
; CHECK-NEXT: (p6) mov r14 = 1
; CHECK-NEXT: cmp.ltu p6, p7 = r16, r15 ;;
; CHECK-NEXT: (p6) mov r8 = 1
; CHECK-NEXT: cmp.eq p6, p7 = r15, r16 ;;
; CHECK-NEXT: (p6) mov r8 = r14
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i1 @isnan(x86_fp80 %x) {
  %b = bitcast x86_fp80 %x to i80
  %c = and i80 %b, 604462909807314587353087
  %d = icmp ugt i80 %c, 604453686435277732577280
  ret i1 %d
}

; CHECK-LABEL: wide:
; CHECK:      mov r14 = r12 ;;
; CHECK-NEXT: adds r15 = 8, r14
; CHECK-NEXT: stfe [r14] = f8 ;;
; CHECK-NEXT: ld2 r15 = [r15] ;;
; CHECK-NEXT: sxt2 r9 = r15
; CHECK-NEXT: ld8 r8 = [r14] ;;
; CHECK-NEXT: shr r10 = r9, 63 ;;
; CHECK-NEXT: mov r11 = r10
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i200 @wide(x86_fp80 %x) {
  %b = bitcast x86_fp80 %x to i80
  %c = sext i80 %b to i200
  ret i200 %c
}

; The low bits of one are the first doubleword of the ten bytes.

; CHECK-LABEL: low:
; CHECK:      ldfe f6 = [r32]
; CHECK-NEXT: mov r14 = r12 ;;
; CHECK-NEXT: stfe [r14] = f6
; CHECK-NEXT: ld8 r8 = [r14]
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i64 @low(ptr %p) {
  %v = load i80, ptr %p
  %t = trunc i80 %v to i64
  ret i64 %t
}

; CHECK-LABEL: lower:
; CHECK:      ldfe f6 = [r32]
; CHECK-NEXT: mov r14 = r12 ;;
; CHECK-NEXT: stfe [r14] = f6
; CHECK-NEXT: ld8 r8 = [r14]
; CHECK-NEXT: br.ret.sptk.many b0 ;;
define i32 @lower(ptr %p) {
  %v = load i80, ptr %p
  %t = trunc i80 %v to i32
  ret i32 %t
}
