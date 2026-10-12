; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   < %s | FileCheck %s

; The global pointer is put back after the label that the unwinder comes to,
; and not before it, whatever debug instructions the landing pad begins with.

; CHECK-LABEL: f:
; CHECK:       br.call.sptk.many b0 = may_throw
; CHECK:       .LBB0_2:
; CHECK-NEXT:  [[PAD:\.Ltmp[0-9]+]]:
; CHECK-NOT:   br.call
; CHECK:       mov r1 = r{{[0-9]+}}
; CHECK:       br.call.sptk.many b0 = handler
; CHECK:       .uleb128 [[PAD]]-.Lfunc_begin0
define i32 @f(i32 %a) personality ptr @__gxx_personality_v0 !dbg !4 {
entry:
  invoke void @may_throw()
          to label %ok unwind label %pad, !dbg !12

ok:
  ret i32 %a, !dbg !12

pad:
    #dbg_value(i32 %a, !9, !DIExpression(), !12)
  %lp = landingpad { ptr, i32 }
          cleanup
    #dbg_value(i32 0, !9, !DIExpression(), !12)
  call void @handler(), !dbg !12
  resume { ptr, i32 } %lp, !dbg !12
}

declare void @may_throw()
declare void @handler()
declare i32 @__gxx_personality_v0(...)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.cpp", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"Dwarf Version", i32 5}
!4 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 1, type: !5, unit: !0, retainedNodes: !8)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7}
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !{!9}
!9 = !DILocalVariable(name: "a", arg: 1, scope: !4, file: !1, line: 1, type: !7)
!12 = !DILocation(line: 1, scope: !4)
