; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -split-machine-functions < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -split-machine-functions \
; RUN:   -filetype=obj < %s -o /dev/null

; The cold part of a split function is a procedure of its own. It is entered
; with the frame the entry prologue made, so its prologue region repeats that
; prologue's directives over no code. The hot part's procedure ends where its
; section does.

declare void @cold_fn(ptr)
declare i64 @hot_fn(i64)
declare i32 @__gxx_personality_v0(...)

define i64 @split(i64 %a) !prof !14 {
; CHECK-LABEL: split:
; CHECK:       .prologue
; CHECK:       .save ar.pfs, r34
; CHECK:       .save rp, r35
; CHECK:       .fframe 64
; CHECK:       .body
; CHECK:       (p6) br.cond.dpnt.many split.cold
; CHECK:       br.ret.sptk.many b0 ;;
; CHECK-NEXT:  .LBB_END0_1:
; CHECK-NEXT:  .endp split
; CHECK-NEXT:  .section .text.split.split,"ax",@progbits
; CHECK-NEXT:  .proc split.cold
; CHECK-NEXT:  split.cold:
; CHECK-NEXT:  .prologue
; CHECK-NEXT:  .save ar.pfs, r34
; CHECK-NEXT:  .save rp, r35
; CHECK-NEXT:  .fframe 64
; CHECK-NEXT:  .body
; CHECK:       br.call.sptk.many b0 = cold_fn ;;
; CHECK:       .size split.cold, .LBB_END0_2-split.cold
; CHECK-NEXT:  .endp split.cold
; CHECK-NEXT:  .section .text.hot.split,"ax",@progbits
; CHECK-NEXT:  .Lfunc_end0:
; CHECK-NEXT:  .size split, .Lfunc_end0-split
entry:
  %buf = alloca [8 x i64]
  %c = icmp eq i64 %a, 0
  br i1 %c, label %cold, label %hot, !prof !15

cold:
  call void @cold_fn(ptr %buf)
  br label %hot

hot:
  %r = call i64 @hot_fn(i64 %a)
  ret i64 %r
}

; A register spilled to memory needs an instruction to be spilled by.
define double @spill(double %a, i64 %b) !prof !14 {
; CHECK-LABEL: spill.cold:
; CHECK-NEXT:  .prologue
; CHECK:       .save.f 0x1
; CHECK-NEXT:  nop 0
; CHECK:       .body
entry:
  %c = icmp eq i64 %b, 0
  br i1 %c, label %cold, label %hot, !prof !15

cold:
  call void @cold_fn(ptr null)
  br label %hot

hot:
  %r = call i64 @hot_fn(i64 %b)
  %f = sitofp i64 %r to double
  %s = fadd double %a, %f
  ret double %s
}

; Language-specific data would be needed for each part, so a function with
; a personality routine is not split.
define i64 @eh(i64 %a) personality ptr @__gxx_personality_v0 !prof !14 {
; CHECK-LABEL: eh:
; CHECK-NOT:   .proc
; CHECK:       br.call.sptk.many b0 = cold_fn ;;
; CHECK-NOT:   .proc
; CHECK:       .handlerdata
; CHECK:       .endp eh
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %cold, label %hot, !prof !15

cold:
  call void @cold_fn(ptr null)
  br label %hot

hot:
  %r = invoke i64 @hot_fn(i64 %a) to label %ok unwind label %lp

ok:
  ret i64 %r

lp:
  %l = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %l
}

!llvm.module.flags = !{!0}
!0 = !{i32 1, !"ProfileSummary", !1}
!1 = !{!2, !3, !4, !5, !6, !7, !8, !9}
!2 = !{!"ProfileFormat", !"InstrProf"}
!3 = !{!"TotalCount", i64 10000}
!4 = !{!"MaxCount", i64 10}
!5 = !{!"MaxInternalCount", i64 1}
!6 = !{!"MaxFunctionCount", i64 1000}
!7 = !{!"NumCounts", i64 3}
!8 = !{!"NumFunctions", i64 5}
!9 = !{!"DetailedSummary", !10}
!10 = !{!11, !12, !13}
!11 = !{i32 10000, i64 100, i32 1}
!12 = !{i32 999900, i64 100, i32 1}
!13 = !{i32 999999, i64 1, i32 2}
!14 = !{!"function_entry_count", i64 7000}
!15 = !{!"branch_weights", i32 0, i32 7000}
