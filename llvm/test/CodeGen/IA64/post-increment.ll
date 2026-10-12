; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A load or a store that is followed by an add to its address does the add
; itself: nine bits of constant, or for a load a register.

define i64 @load8(ptr %p, ptr %q) {
; CHECK-LABEL: load8:
; CHECK:       ld8 r8 = [r32], 8 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load i64, ptr %p
  %n = getelementptr i8, ptr %p, i64 8
  store ptr %n, ptr %q
  ret i64 %v
}

define i64 @load_narrow(ptr %p, ptr %q) {
; CHECK-LABEL: load_narrow:
; CHECK:       ld2 r8 = [r32], -256 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load i16, ptr %p
  %n = getelementptr i8, ptr %p, i64 -256
  store ptr %n, ptr %q
  %z = zext i16 %v to i64
  ret i64 %z
}

; The load that extends the sign is the load and then the extension.
define i64 @load_signed(ptr %p, ptr %q) {
; CHECK-LABEL: load_signed:
; CHECK:       ld4 r14 = [r32], 4 ;;
; CHECK:       sxt4 r8 = r14
  %v = load i32, ptr %p
  %n = getelementptr i8, ptr %p, i64 4
  store ptr %n, ptr %q
  %z = sext i32 %v to i64
  ret i64 %z
}

define i64 @load_register(ptr %p, ptr %q, i64 %i) {
; CHECK-LABEL: load_register:
; CHECK:       ld8 r8 = [r32], r34 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load i64, ptr %p
  %n = getelementptr i8, ptr %p, i64 %i
  store ptr %n, ptr %q
  ret i64 %v
}

; 256 is one more than the nine bits hold.
define i64 @load_far(ptr %p, ptr %q) {
; CHECK-LABEL: load_far:
; CHECK:       ld8 r8 = [r32]
; CHECK-NEXT:  adds r14 = 256, r32 ;;
  %v = load i64, ptr %p
  %n = getelementptr i8, ptr %p, i64 256
  store ptr %n, ptr %q
  ret i64 %v
}

define double @load_double(ptr %p, ptr %q) {
; CHECK-LABEL: load_double:
; CHECK:       ldfd f8 = [r32], 8 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load double, ptr %p
  %n = getelementptr i8, ptr %p, i64 8
  store ptr %n, ptr %q
  ret double %v
}

define float @load_float(ptr %p, ptr %q, i64 %i) {
; CHECK-LABEL: load_float:
; CHECK:       ldfs f8 = [r32], r34 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load float, ptr %p
  %n = getelementptr i8, ptr %p, i64 %i
  store ptr %n, ptr %q
  ret float %v
}

define x86_fp80 @load_extended(ptr %p, ptr %q) {
; CHECK-LABEL: load_extended:
; CHECK:       ldfe f8 = [r32], 16 ;;
; CHECK-NEXT:  st8 [r33] = r32
  %v = load x86_fp80, ptr %p
  %n = getelementptr i8, ptr %p, i64 16
  store ptr %n, ptr %q
  ret x86_fp80 %v
}

define ptr @store8(ptr %p, i64 %v) {
; CHECK-LABEL: store8:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  st8 [r8] = r33, 8
  store i64 %v, ptr %p
  %n = getelementptr i8, ptr %p, i64 8
  ret ptr %n
}

define ptr @store_narrow(ptr %p, i64 %v) {
; CHECK-LABEL: store_narrow:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  st1 [r8] = r33, -1
  %t = trunc i64 %v to i8
  store i8 %t, ptr %p
  %n = getelementptr i8, ptr %p, i64 -1
  ret ptr %n
}

; A store has no form that adds a register.
define ptr @store_register(ptr %p, i64 %v, i64 %i) {
; CHECK-LABEL: store_register:
; CHECK:       add r8 = r32, r34
; CHECK-NEXT:  st8 [r32] = r33
  store i64 %v, ptr %p
  %n = getelementptr i8, ptr %p, i64 %i
  ret ptr %n
}

define ptr @store_double(ptr %p, double %v) {
; CHECK-LABEL: store_double:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  stfd [r8] = f8, 8
  store double %v, ptr %p
  %n = getelementptr i8, ptr %p, i64 8
  ret ptr %n
}

define ptr @store_float(ptr %p, float %v) {
; CHECK-LABEL: store_float:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  stfs [r8] = f8, 4
  store float %v, ptr %p
  %n = getelementptr i8, ptr %p, i64 4
  ret ptr %n
}

define ptr @store_extended(ptr %p, x86_fp80 %v) {
; CHECK-LABEL: store_extended:
; CHECK:       mov r8 = r32 ;;
; CHECK-NEXT:  stfe [r8] = f8, 16
  store x86_fp80 %v, ptr %p
  %n = getelementptr i8, ptr %p, i64 16
  ret ptr %n
}

; The copy of a string a byte at a time is a load, a store and the count.
define void @copy(ptr %d, ptr %s, i64 %n) {
; CHECK-LABEL: copy:
; CHECK:       ld1 r14 = [r33], 1
; CHECK-NEXT:  adds r34 = -1, r34 ;;
; CHECK-NEXT:  st1 [r32] = r14, 1
entry:
  %e = icmp eq i64 %n, 0
  br i1 %e, label %x, label %l
l:
  %dp = phi ptr [ %d, %entry ], [ %dn, %l ]
  %sp = phi ptr [ %s, %entry ], [ %sn, %l ]
  %i = phi i64 [ %n, %entry ], [ %in, %l ]
  %b = load i8, ptr %sp
  store i8 %b, ptr %dp
  %dn = getelementptr i8, ptr %dp, i64 1
  %sn = getelementptr i8, ptr %sp, i64 1
  %in = add i64 %i, -1
  %c = icmp eq i64 %in, 0
  br i1 %c, label %x, label %l
x:
  ret void
}
