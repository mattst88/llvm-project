; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; An atomic operation is the instruction SelectionDAG makes of it: a compare and
; exchange through ar.ccv, an exchange, a fetch and add where the increment is
; one the instruction has, and a load or a store that says how it is ordered.
; Nothing knows yet that a narrow compare and exchange zero-extends what it
; loads, so the value is extended again before it is compared.

; CHECK-LABEL: cas64:
; CHECK:         mf
; CHECK-NEXT:    mov ar.ccv = r33 ;;
; CHECK-NEXT:    cmpxchg8.acq r8 = [r32], r34, ar.ccv
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i64 @cas64(ptr %p, i64 %c, i64 %n) {
  %r = cmpxchg ptr %p, i64 %c, i64 %n seq_cst seq_cst
  %v = extractvalue { i64, i1 } %r, 0
  ret i64 %v
}

; CHECK-LABEL: cas8:
; CHECK:         zxt1 r14 = r33
; CHECK-NEXT:    mov r8 = r0 ;;
; CHECK-NEXT:    mov ar.ccv = r14 ;;
; CHECK-NEXT:    cmpxchg1.acq r15 = [r32], r34, ar.ccv ;;
; CHECK-NEXT:    zxt1 r15 = r15 ;;
; CHECK-NEXT:    cmp.eq p6, p7 = r15, r14 ;;
; CHECK-NEXT:    (p6) mov r8 = 1
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i1 @cas8(ptr %p, i8 %c, i8 %n) {
  %r = cmpxchg ptr %p, i8 %c, i8 %n acquire acquire
  %v = extractvalue { i8, i1 } %r, 1
  ret i1 %v
}

; CHECK-LABEL: swap:
; CHECK:         mf
; CHECK-NEXT:    xchg2 r8 = [r32], r33
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i16 @swap(ptr %p, i16 %n) {
  %r = atomicrmw xchg ptr %p, i16 %n seq_cst
  ret i16 %r
}

; CHECK-LABEL: add:
; CHECK:         mf
; CHECK-NEXT:    fetchadd4.acq r8 = [r32], 4
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i32 @add(ptr %p) {
  %r = atomicrmw add ptr %p, i32 4 seq_cst
  ret i32 %r
}

; CHECK-LABEL: sub:
; CHECK:         fetchadd8.rel r8 = [r32], -1
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i64 @sub(ptr %p) {
  %r = atomicrmw sub ptr %p, i64 1 release
  ret i64 %r
}

; CHECK-LABEL: or:
; CHECK:         ld4.acq r8 = [r32] ;;
; CHECK:         addp4 r15 = r8, r0
; CHECK-NEXT:    or r14 = r8, r33 ;;
; CHECK-NEXT:    mov ar.ccv = r15 ;;
; CHECK-NEXT:    cmpxchg4.acq r8 = [r32], r14, ar.ccv ;;
; CHECK-NEXT:    addp4 r14 = r8, r0 ;;
; CHECK-NEXT:    cmp.eq p7, p6 = r14, r15
; CHECK-NEXT:    (p6) br.cond.dptk.few .LBB5_1{{$}}
; CHECK:         br.ret.sptk.many b0 ;;
define i32 @or(ptr %p, i32 %x) {
  %r = atomicrmw or ptr %p, i32 %x monotonic
  ret i32 %r
}

; CHECK-LABEL: load:
; CHECK:         ld4.acq r8 = [r32]
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define i32 @load(ptr %p) {
  %r = load atomic i32, ptr %p seq_cst, align 4
  ret i32 %r
}

; CHECK-LABEL: store:
; CHECK:         getf.d r14 = f8
; CHECK-NEXT:    mov r15 = 1
; CHECK-NEXT:    st4.rel [r32] = r33
; CHECK-NEXT:    mf ;;
; CHECK-NEXT:    st1.rel [r32] = r15
; CHECK-NEXT:    st8.rel [r34] = r14
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define void @store(ptr %p, i32 %x, ptr %q, double %d) {
  store atomic i32 %x, ptr %p seq_cst, align 4
  store atomic i8 1, ptr %p release, align 1
  store atomic double %d, ptr %q monotonic, align 8
  ret void
}

; CHECK-LABEL: fence:
; CHECK:         mf
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
define void @fence() {
  fence acquire
  fence seq_cst
  fence syncscope("singlethread") seq_cst
  ret void
}
