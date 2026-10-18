; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-dwarfdump --debug-info - | FileCheck %s

; A parameter arrives in a stacked register, which the function sees under
; the same name for as long as it runs, and whose DWARF number is the number
; of the register and needs DW_OP_regx from r32 on. The first parameter stays
; where it came. The second is in r33 until r33 is given the address of the
; local variable, by the instruction in the first slot of the second bundle:
; the address of a slot is that of its bundle with the slot number added, so
; the range ends at 0x11. What is in memory is found from the stack pointer,
; which is the frame base.

; CHECK:      DW_TAG_subprogram
; CHECK:        DW_AT_frame_base (DW_OP_reg12 R12)
; CHECK:        DW_AT_name ("f")
; CHECK:      DW_TAG_formal_parameter
; CHECK-NEXT:   DW_AT_location (DW_OP_regx R32)
; CHECK-NEXT:   DW_AT_name ("a")
; CHECK:      DW_TAG_formal_parameter
; CHECK-NEXT:   DW_AT_location
; CHECK-NEXT:     [0x0000000000000000, 0x0000000000000011): DW_OP_regx R33)
; CHECK-NEXT:   DW_AT_name ("b")
; CHECK:      DW_TAG_variable
; CHECK-NEXT:   DW_AT_location (DW_OP_fbreg +28)
; CHECK-NEXT:   DW_AT_name ("local")

define i32 @f(i32 %a, i32 %b) !dbg !8 {
entry:
  %local = alloca i32, align 4
    #dbg_value(i32 %a, !13, !DIExpression(), !16)
    #dbg_value(i32 %b, !14, !DIExpression(), !16)
    #dbg_declare(ptr %local, !15, !DIExpression(), !16)
  %add = add nsw i32 %b, %a, !dbg !17
  store i32 %add, ptr %local, align 4, !dbg !18
  %call = call i32 @ext(ptr %local), !dbg !19
  %0 = load i32, ptr %local, align 4, !dbg !20
  %mul = mul nsw i32 %0, %a, !dbg !21
  ret i32 %mul, !dbg !22
}

declare i32 @ext(ptr)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: true, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!8 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 2, type: !9, scopeLine: 2, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !12)
!9 = !DISubroutineType(types: !10)
!10 = !{!11, !11, !11}
!11 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!12 = !{!13, !14, !15}
!13 = !DILocalVariable(name: "a", arg: 1, scope: !8, file: !1, line: 2, type: !11)
!14 = !DILocalVariable(name: "b", arg: 2, scope: !8, file: !1, line: 2, type: !11)
!15 = !DILocalVariable(name: "local", scope: !8, file: !1, line: 3, type: !11)
!16 = !DILocation(line: 0, scope: !8)
!17 = !DILocation(line: 3, column: 17, scope: !8)
!18 = !DILocation(line: 3, column: 7, scope: !8)
!19 = !DILocation(line: 4, column: 3, scope: !8)
!20 = !DILocation(line: 5, column: 10, scope: !8)
!21 = !DILocation(line: 5, column: 16, scope: !8)
!22 = !DILocation(line: 5, column: 3, scope: !8)
