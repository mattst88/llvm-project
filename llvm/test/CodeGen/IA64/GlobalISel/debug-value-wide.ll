; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A value that is wider than a register is in no one register after the
; legalizer, and the debug instruction that names it whole is no reason to
; give the function up.

; CHECK-LABEL: wide:
; CHECK:       ld8 r{{[0-9]+}} = [r32]
; CHECK:       br.ret.sptk.many b0
define i128 @wide(ptr %p, i64 %k) !dbg !4 {
  %x = load i128, ptr %p
    #dbg_value(i128 %x, !9, !DIExpression(), !12)
  %e = sext i64 %k to i128
  %y = add i128 %x, %e
  ret i128 %y, !dbg !12
}

; CHECK-LABEL: vec:
; CHECK:       ld4 r{{[0-9]+}} = [r33]
; CHECK:       br.ret.sptk.many b0
define void @vec(ptr %r, ptr %a) !dbg !13 {
  %x = load <4 x i32>, ptr %a
    #dbg_value(<4 x i32> %x, !15, !DIExpression(), !18)
  %y = add <4 x i32> %x, %x
  store <4 x i32> %y, ptr %r
  ret void, !dbg !18
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"Dwarf Version", i32 5}
!4 = distinct !DISubprogram(name: "wide", scope: !1, file: !1, line: 1, type: !5, unit: !0, retainedNodes: !8)
!5 = !DISubroutineType(types: !6)
!6 = !{!7}
!7 = !DIBasicType(name: "__int128", size: 128, encoding: DW_ATE_signed)
!8 = !{!9}
!9 = !DILocalVariable(name: "x", scope: !4, file: !1, line: 1, type: !7)
!12 = !DILocation(line: 1, scope: !4)
!13 = distinct !DISubprogram(name: "vec", scope: !1, file: !1, line: 2, type: !5, unit: !0, retainedNodes: !14)
!14 = !{!15}
!15 = !DILocalVariable(name: "x", scope: !13, file: !1, line: 2, type: !16)
!16 = !DICompositeType(tag: DW_TAG_array_type, baseType: !17, size: 128, flags: DIFlagVector, elements: !19)
!17 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!18 = !DILocation(line: 2, scope: !13)
!19 = !{!20}
!20 = !DISubrange(count: 4)
