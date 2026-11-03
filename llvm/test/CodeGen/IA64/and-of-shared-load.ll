; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; The low byte of a loaded halfword whose other byte is also used is an ld1
; of its own, not a zxt1 after the ld2: an interpreter's dispatch indexes a
; table with the opcode, and the zxt1 would lengthen the chain to its branch.

@t = external global [256 x ptr]

; CHECK-LABEL: dispatch:
; CHECK: ld1 [[OP:r[0-9]+]] = [r32]
; CHECK-NOT: zxt1
; CHECK: shladd {{r[0-9]+}} = [[OP]], 3,
; CHECK: br.cond.sptk.few b7
define void @dispatch(ptr %pc, ptr %s, i64 %v) {
  %w = load i16, ptr %pc, align 2
  %op = trunc i16 %w to i8
  %opx = zext i8 %op to i64
  %arg = lshr i16 %w, 8
  %argx = zext i16 %arg to i64
  %slot = getelementptr inbounds [256 x ptr], ptr @t, i64 0, i64 %opx
  %tgt = load ptr, ptr %slot
  %q = getelementptr i64, ptr %s, i64 %argx
  store i64 %v, ptr %q
  indirectbr ptr %tgt, []
}

; A wider load is not split: the narrow load would not be advanced with it,
; and would wait for the same cache miss.
; CHECK-LABEL: low_byte_of_word:
; CHECK: ld4 [[W:r[0-9]+]] = [r32]
; CHECK-NOT: ld1
; CHECK: zxt1 {{r[0-9]+}} = [[W]]
define i64 @low_byte_of_word(ptr %p) {
  %w = load i32, ptr %p
  %lo = and i32 %w, 255
  %hi = lshr i32 %w, 8
  %a = zext i32 %lo to i64
  %b = zext i32 %hi to i64
  %r = add i64 %a, %b
  ret i64 %r
}

; A volatile load is not split.
; CHECK-LABEL: volatile_half:
; CHECK: ld2
; CHECK: zxt1
; CHECK-NOT: ld1
; CHECK: br.ret
define i64 @volatile_half(ptr %p) {
  %w = load volatile i16, ptr %p
  %lo = and i16 %w, 255
  %hi = lshr i16 %w, 8
  %a = zext i16 %lo to i64
  %b = zext i16 %hi to i64
  %r = mul i64 %a, %b
  ret i64 %r
}
