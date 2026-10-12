; Check the IA-64 shadow mapping.  The top three address bits select a region,
; so the shadow of the low 2^40 bytes of each region is laid out in region 0,
; indexed by the region number, with no offset:
;   shadow = ((addr >> 61) << 37) | ((addr & (2^40 - 1)) >> 3)
; This matches MEM_TO_SHADOW in compiler-rt/lib/asan/asan_mapping_ia64.h.
;
; RUN: opt < %s -passes=asan -S -mtriple=ia64-unknown-linux-gnu | FileCheck %s
; RUN: opt < %s -passes=asan -S -mtriple=ia64-unknown-linux-gnu \
; RUN:     -asan-mapping-scale=5 | FileCheck %s --check-prefix=SCALE5

define i32 @read(ptr %a) sanitize_address {
entry:
  %tmp1 = load i32, ptr %a, align 4
  ret i32 %tmp1
}
; CHECK-LABEL: @read
; CHECK:      [[ADDR:%.*]] = ptrtoint ptr %a to i64
; CHECK-NEXT: [[REGION:%.*]] = lshr i64 [[ADDR]], 61
; CHECK-NEXT: [[BASE:%.*]] = shl i64 [[REGION]], 37
; CHECK-NEXT: [[WINDOW:%.*]] = and i64 [[ADDR]], 1099511627775
; CHECK-NEXT: [[INDEX:%.*]] = lshr i64 [[WINDOW]], 3
; CHECK-NEXT: [[SHADOW:%.*]] = or i64 [[BASE]], [[INDEX]]
; CHECK-NEXT: inttoptr i64 [[SHADOW]] to ptr
; CHECK:      ret

; SCALE5-LABEL: @read
; SCALE5:      [[REGION:%.*]] = lshr i64 [[ADDR:%.*]], 61
; SCALE5-NEXT: [[BASE:%.*]] = shl i64 [[REGION]], 35
; SCALE5-NEXT: [[WINDOW:%.*]] = and i64 [[ADDR]], 1099511627775
; SCALE5-NEXT: [[INDEX:%.*]] = lshr i64 [[WINDOW]], 5
; SCALE5-NEXT: or i64 [[BASE]], [[INDEX]]

declare void @escape(ptr)

define void @frame() sanitize_address {
entry:
  %x = alloca [10 x i8], align 1
  call void @escape(ptr %x)
  ret void
}
; The stack redzones are poisoned through the same mapping.
; CHECK-LABEL: @frame
; CHECK:      [[FP:%.*]] = ptrtoint ptr {{.*}} to i64
; CHECK:      [[REGION:%.*]] = lshr i64 [[FP]], 61
; CHECK-NEXT: [[BASE:%.*]] = shl i64 [[REGION]], 37
; CHECK-NEXT: [[WINDOW:%.*]] = and i64 [[FP]], 1099511627775
; CHECK-NEXT: [[INDEX:%.*]] = lshr i64 [[WINDOW]], 3
; CHECK-NEXT: or i64 [[BASE]], [[INDEX]]
