; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -O0 -filetype=obj < %s -o %t
; RUN: llvm-dwarfdump --debug-info %t | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -O2 -filetype=obj < %s -o %t
; RUN: llvm-dwarfdump --debug-info %t | FileCheck %s

; A parameter of 32 bits arrives in a register of 64, and the translator
; describes it by the truncation of the register. Legalization widens what
; uses the truncation and then has no use for it; the parameter is still
; where it came, and its location says so, as the low half of the register.

; CHECK:      DW_TAG_formal_parameter
; CHECK-NEXT:   DW_AT_location (DW_OP_bregx R32+0, DW_OP_convert ({{.*}}) "DW_ATE_unsigned_64", DW_OP_convert ({{.*}}) "DW_ATE_unsigned_32", DW_OP_stack_value)
; CHECK-NEXT:   DW_AT_name ("a")
; CHECK:      DW_TAG_formal_parameter
; CHECK-NEXT:   DW_AT_location
; CHECK-NEXT:     [0x{{[0-9a-f]+}}, 0x{{[0-9a-f]+}}): DW_OP_breg{{.*}}, DW_OP_convert ({{.*}}) "DW_ATE_unsigned_64", DW_OP_convert ({{.*}}) "DW_ATE_unsigned_32", DW_OP_stack_value)
; CHECK-NEXT:   DW_AT_name ("b")

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
