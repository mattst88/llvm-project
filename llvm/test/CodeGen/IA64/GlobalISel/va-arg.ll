; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -stop-after=legalizer -simplify-mir < %s \
; RUN:   | FileCheck %s

; The next argument is read from where the list is, and the list moves past
; the slots the argument took: one of eight bytes for an integer however
; narrow, and two at the next address that is a multiple of sixteen for a
; value that is aligned so.

define i64 @whole(ptr %ap) {
; CHECK-LABEL: name: whole
; CHECK:       [[AP:%[0-9]+]]:_(p0) = COPY $r32
; CHECK:       [[LIST:%[0-9]+]]:_(p0) = G_LOAD [[AP]](p0) :: (load (p0))
; CHECK:       [[EIGHT:%[0-9]+]]:_(s64) = G_CONSTANT i64 8
; CHECK:       [[NEXT:%[0-9]+]]:_(p0) = G_PTR_ADD [[LIST]], [[EIGHT]](s64)
; CHECK:       G_STORE [[NEXT]](p0), [[AP]](p0) :: (store (p0))
; CHECK:       G_LOAD [[LIST]](p0) :: (load (s64))
  %v = va_arg ptr %ap, i64
  ret i64 %v
}

define i32 @narrow(ptr %ap) {
; CHECK-LABEL: name: narrow
; CHECK:       [[AP:%[0-9]+]]:_(p0) = COPY $r32
; CHECK:       [[LIST:%[0-9]+]]:_(p0) = G_LOAD [[AP]](p0) :: (load (p0))
; CHECK:       [[EIGHT:%[0-9]+]]:_(s64) = G_CONSTANT i64 8
; CHECK:       [[NEXT:%[0-9]+]]:_(p0) = G_PTR_ADD [[LIST]], [[EIGHT]](s64)
; CHECK:       G_STORE [[NEXT]](p0), [[AP]](p0) :: (store (p0))
; CHECK:       G_LOAD [[LIST]](p0) :: (load (s64))
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

define x86_fp80 @extended(ptr %ap) {
; CHECK-LABEL: name: extended
; CHECK:       [[AP:%[0-9]+]]:_(p0) = COPY $r32
; CHECK:       [[LIST:%[0-9]+]]:_(p0) = G_LOAD [[AP]](p0) :: (load (p0))
; CHECK:       [[FIFTEEN:%[0-9]+]]:_(s64) = G_CONSTANT i64 15
; CHECK:       G_PTR_ADD [[LIST]], [[FIFTEEN]](s64)
; CHECK:       [[MASK:%[0-9]+]]:_(s64) = G_CONSTANT i64 -16
; CHECK:       G_AND %{{[0-9]+}}, [[MASK]]
; CHECK:       [[SIXTEEN:%[0-9]+]]:_(s64) = G_CONSTANT i64 16
; CHECK:       [[NEXT:%[0-9]+]]:_(p0) = G_PTR_ADD [[ALIGNED:%[0-9]+]], [[SIXTEEN]](s64)
; CHECK:       G_STORE [[NEXT]](p0), [[AP]](p0) :: (store (p0))
; CHECK:       G_LOAD [[ALIGNED]](p0) :: (load (s80), align 16)
  %v = va_arg ptr %ap, x86_fp80
  ret x86_fp80 %v
}
