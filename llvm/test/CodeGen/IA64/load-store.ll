; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

define i64 @load8(ptr %p) {
; CHECK-LABEL: load8:
; CHECK:       ld8 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i64, ptr %p
  ret i64 %v
}

; A narrow load clears the bits above it, which is what a result nobody
; asked to have extended may as well have.

define i32 @load4(ptr %p) {
; CHECK-LABEL: load4:
; CHECK:       ld4 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i32, ptr %p
  ret i32 %v
}

define zeroext i32 @load4_zext(ptr %p) {
; CHECK-LABEL: load4_zext:
; CHECK:       ld4 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i32, ptr %p
  ret i32 %v
}

define i64 @load2_zext(ptr %p) {
; CHECK-LABEL: load2_zext:
; CHECK:       ld2 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i16, ptr %p
  %e = zext i16 %v to i64
  ret i64 %e
}

define i64 @load1_zext(ptr %p) {
; CHECK-LABEL: load1_zext:
; CHECK:       ld1 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i8, ptr %p
  %e = zext i8 %v to i64
  ret i64 %e
}

define i64 @load_bit(ptr %p) {
; CHECK-LABEL: load_bit:
; CHECK:       ld1 r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  %v = load i1, ptr %p
  %e = zext i1 %v to i64
  ret i64 %e
}

define void @store8(ptr %p, i64 %v) {
; CHECK-LABEL: store8:
; CHECK:       st8 [r32] = r33
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  store i64 %v, ptr %p
  ret void
}

define void @store4(ptr %p, i32 %v) {
; CHECK-LABEL: store4:
; CHECK:       st4 [r32] = r33
  store i32 %v, ptr %p
  ret void
}

define void @store2(ptr %p, i16 %v) {
; CHECK-LABEL: store2:
; CHECK:       st2 [r32] = r33
  store i16 %v, ptr %p
  ret void
}

define void @store1(ptr %p, i8 %v) {
; CHECK-LABEL: store1:
; CHECK:       st1 [r32] = r33
  store i8 %v, ptr %p
  ret void
}

define void @store_truncated(ptr %p, i64 %v) {
; CHECK-LABEL: store_truncated:
; CHECK:       st1 [r32] = r33
  %t = trunc i64 %v to i8
  store i8 %t, ptr %p
  ret void
}

define void @store_zero(ptr %p) {
; CHECK-LABEL: store_zero:
; CHECK:       st8 [r32] = r0
  store i64 0, ptr %p
  ret void
}

; There is no displacement, so the address is computed.

define i64 @load_offset(ptr %p) {
; CHECK-LABEL: load_offset:
; CHECK:       adds [[A:r[0-9]+]] = 16, r32 ;;
; CHECK-NEXT:  ld8 r8 = [[[A]]]
  %q = getelementptr i64, ptr %p, i64 2
  %v = load i64, ptr %q
  ret i64 %v
}

define i64 @load_indexed(ptr %p, i64 %i) {
; CHECK-LABEL: load_indexed:
; CHECK:       add [[A:r[0-9]+]] = r32, r33 ;;
; CHECK-NEXT:  ld1 r8 = [[[A]]]
  %q = getelementptr i8, ptr %p, i64 %i
  %v = load i8, ptr %q
  %e = zext i8 %v to i64
  ret i64 %e
}

; A store and the load after it are in one group: memory is no register.

define i64 @store_load(ptr %p, ptr %q, i64 %v) {
; CHECK-LABEL: store_load:
; CHECK:       st8 [r32] = r34
; CHECK-NEXT:  ld8 r8 = [r33]
; CHECK-NEXT:  br.ret.sptk.many b0 ;;
  store i64 %v, ptr %p
  %r = load i64, ptr %q
  ret i64 %r
}
