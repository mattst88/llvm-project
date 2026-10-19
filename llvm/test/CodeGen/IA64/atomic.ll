; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; A load that is atomic is ordered before what follows it and a store after
; what precedes it, which is all that any ordering but the strongest asks of
; them. A store that is to be ordered before the loads that follow has a
; fence after it.

define i64 @load(ptr %p) {
; CHECK-LABEL: load:
; CHECK:       ld8.acq r8 = [r32]
; CHECK-NEXT:  br.ret.sptk.many b0
  %v = load atomic i64, ptr %p seq_cst, align 8
  ret i64 %v
}

define signext i8 @load_narrow(ptr %p) {
; CHECK-LABEL: load_narrow:
; CHECK:       ld1.acq r14 = [r32] ;;
; CHECK-NEXT:  sxt1 r8 = r14
  %v = load atomic i8, ptr %p monotonic, align 1
  ret i8 %v
}

define void @store(ptr %p, i64 %v) {
; CHECK-LABEL: store:
; CHECK:       st8.rel [r32] = r33
; CHECK-NEXT:  mf
; CHECK-NEXT:  br.ret.sptk.many b0
  store atomic i64 %v, ptr %p seq_cst, align 8
  ret void
}

define void @store_release(ptr %p, i16 %v) {
; CHECK-LABEL: store_release:
; CHECK:       st2.rel [r32] = r33
; CHECK-NEXT:  br.ret.sptk.many b0
  store atomic i16 %v, ptr %p release, align 2
  ret void
}

; An exchange is ordered before what follows it. The fence in front of it
; orders it after what came before.

define i64 @exchange(ptr %p, i64 %v) {
; CHECK-LABEL: exchange:
; CHECK:       mf
; CHECK-NEXT:  xchg8 r8 = [r32], r33
  %o = atomicrmw xchg ptr %p, i64 %v seq_cst
  ret i64 %o
}

define i8 @exchange_acquire(ptr %p, i8 %v) {
; CHECK-LABEL: exchange_acquire:
; CHECK-NOT:   mf
; CHECK:       xchg1 r8 = [r32], r33
  %o = atomicrmw xchg ptr %p, i8 %v acquire
  ret i8 %o
}

; What is compared with goes to ar.ccv, and all of it is compared with what
; is read, which has zeroes above its width.

define i64 @compare_exchange(ptr %p, i64 %c, i64 %n) {
; CHECK-LABEL: compare_exchange:
; CHECK:       mf
; CHECK-NEXT:  mov ar.ccv = r33 ;;
; CHECK-NEXT:  cmpxchg8.acq r8 = [r32], r34, ar.ccv
  %r = cmpxchg ptr %p, i64 %c, i64 %n seq_cst seq_cst
  %o = extractvalue {i64, i1} %r, 0
  ret i64 %o
}

define i1 @compare_exchange_narrow(ptr %p, i8 %c, i8 %n) {
; CHECK-LABEL: compare_exchange_narrow:
; CHECK:       zxt1 r14 = r33
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  mov ar.ccv = r14 ;;
; CHECK-NEXT:  cmpxchg1.acq r15 = [r32], r34, ar.ccv ;;
; CHECK-NEXT:  cmp.eq p6, p7 = r15, r14
  %r = cmpxchg ptr %p, i8 %c, i8 %n acquire monotonic
  %o = extractvalue {i8, i1} %r, 1
  ret i1 %o
}

; fetchadd adds 1, 4, 8 or 16 or takes one of them away, from four bytes or
; eight, and can be ordered either way round.

define i64 @add(ptr %p) {
; CHECK-LABEL: add:
; CHECK:       mf
; CHECK-NEXT:  fetchadd8.acq r8 = [r32], 1
  %o = atomicrmw add ptr %p, i64 1 seq_cst
  ret i64 %o
}

define i32 @sub_release(ptr %p) {
; CHECK-LABEL: sub_release:
; CHECK-NOT:   mf
; CHECK:       fetchadd4.rel r8 = [r32], -4
  %o = atomicrmw sub ptr %p, i32 4 release
  ret i32 %o
}

define i32 @add_relaxed(ptr %p) {
; CHECK-LABEL: add_relaxed:
; CHECK-NOT:   mf
; CHECK:       fetchadd4.acq r8 = [r32], -16
  %o = atomicrmw add ptr %p, i32 -16 monotonic
  ret i32 %o
}

; Anything else is a compare and exchange until it succeeds.

define i64 @add_other(ptr %p) {
; CHECK-LABEL: add_other:
; CHECK:       ld8.acq r8 = [r32]{{$}}
; CHECK:       mov ar.ccv = r8
; CHECK-NEXT:  adds r15 = 3, r8
; CHECK-NEXT:  mov r14 = r8 ;;
; CHECK-NEXT:  cmpxchg8.acq r8 = [r32], r15, ar.ccv ;;
; CHECK-NEXT:  cmp.eq p7, p6 = r8, r14
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB
  %o = atomicrmw add ptr %p, i64 3 seq_cst
  ret i64 %o
}

define i16 @add_narrow(ptr %p) {
; CHECK-LABEL: add_narrow:
; CHECK:       ld2.acq r8 = [r32] ;;
; CHECK:       cmpxchg2.acq r8 = [r32], r14, ar.ccv ;;
  %o = atomicrmw add ptr %p, i16 1 seq_cst
  ret i16 %o
}

define i32 @maximum(ptr %p, i32 %v) {
; CHECK-LABEL: maximum:
; CHECK:       cmp4.lt p6, p7 = r33, r8 ;;
; CHECK:       cmpxchg4.acq r8 = [r32], {{r[0-9]+}}, ar.ccv ;;
; CHECK-NEXT:  cmp4.eq p7, p6 = r8, r14
  %o = atomicrmw max ptr %p, i32 %v seq_cst
  ret i32 %o
}

define void @fence() {
; CHECK-LABEL: fence:
; CHECK:       mf
; CHECK-NEXT:  br.ret.sptk.many b0
  fence seq_cst
  ret void
}

define void @fence_acquire() {
; CHECK-LABEL: fence_acquire:
; CHECK-NOT:   mf
; CHECK:       br.ret.sptk.many b0
  fence acquire
  ret void
}

define void @fence_single_thread() {
; CHECK-LABEL: fence_single_thread:
; CHECK-NOT:   mf
; CHECK:       br.ret.sptk.many b0
  fence syncscope("singlethread") seq_cst
  ret void
}

; A fence that does nothing leaves what was before it in front of what comes
; after it, whichever of its results the instruction before it is ordered by.

define i64 @fence_after_load(ptr %p, ptr %q) {
; CHECK-LABEL: fence_after_load:
; CHECK:       ld8 r8 = [r32]
; CHECK-NOT:   mf
; CHECK:       st8 [r33] = r0
; CHECK:       br.ret.sptk.many b0
  %v = load volatile i64, ptr %p
  fence acquire
  store volatile i64 0, ptr %q
  ret i64 %v
}

; Wider than a register is a call.

define i128 @wide(ptr %p, i128 %v) {
; CHECK-LABEL: wide:
; CHECK:       br.call.sptk.many b0 = __atomic_fetch_add_16
  %o = atomicrmw add ptr %p, i128 %v seq_cst
  ret i128 %o
}
