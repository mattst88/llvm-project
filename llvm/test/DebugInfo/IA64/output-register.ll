; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s -o %t
; RUN: llvm-dwarfdump --debug-info %t | FileCheck %s
; RUN: llvm-dwarfdump --verify %t
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=ASM

; A value that is computed for a call is in an output register, and which
; register that is depends on how many inputs and locals the frame has: here
; one and three, so that the first output is r36. The location of a variable
; names the register by the number it has in the frame.

; ASM: alloc r{{[0-9]+}} = ar.pfs, 1, 3, 2, 0

; CHECK:      DW_TAG_variable
; CHECK-NEXT:   DW_AT_location
; CHECK-NEXT:     [0x{{[0-9a-f]+}}, 0x{{[0-9a-f]+}}): DW_OP_regx R37)
; CHECK-NEXT:   DW_AT_name ("y")
; CHECK:      DW_TAG_variable
; CHECK-NEXT:   DW_AT_location
; CHECK-NEXT:     [0x{{[0-9a-f]+}}, 0x{{[0-9a-f]+}}): DW_OP_regx R36)
; CHECK-NEXT:   DW_AT_name ("z")

define void @f(i64 %a) !dbg !8 {
entry:
    #dbg_value(i64 %a, !13, !DIExpression(), !16)
  %mul = mul nsw i64 %a, 3, !dbg !17
  %call = tail call i64 @k(), !dbg !17
  %add = add nsw i64 %call, %mul, !dbg !17
    #dbg_value(i64 %add, !14, !DIExpression(), !16)
  %xor = xor i64 %add, 5, !dbg !18
    #dbg_value(i64 %xor, !15, !DIExpression(), !16)
  tail call void @g(i64 %xor, i64 %add), !dbg !19
  tail call void @g(i64 1, i64 2), !dbg !20
  ret void, !dbg !21
}

declare i64 @k()
declare void @g(i64, i64)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "o.c", directory: "/")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!8 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 3, type: !9, scopeLine: 3, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !12)
!9 = !DISubroutineType(types: !10)
!10 = !{null, !11}
!11 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!12 = !{!13, !14, !15}
!13 = !DILocalVariable(name: "a", arg: 1, scope: !8, file: !1, line: 3, type: !11)
!14 = !DILocalVariable(name: "y", scope: !8, file: !1, line: 4, type: !11)
!15 = !DILocalVariable(name: "z", scope: !8, file: !1, line: 5, type: !11)
!16 = !DILocation(line: 0, scope: !8)
!17 = !DILocation(line: 4, column: 14, scope: !8)
!18 = !DILocation(line: 5, column: 14, scope: !8)
!19 = !DILocation(line: 6, column: 3, scope: !8)
!20 = !DILocation(line: 7, column: 3, scope: !8)
!21 = !DILocation(line: 8, column: 1, scope: !8)
