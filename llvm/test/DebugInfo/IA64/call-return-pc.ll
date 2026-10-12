; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llvm-dwarfdump --debug-info %t.o | FileCheck %s --check-prefix=DWARF

; A call returns to the bundle after its own, and that is the address
; DW_AT_call_return_pc has: not the slot of the instruction after the call,
; which here is an I-unit one that the second slot of its bundle holds. What
; says so is still a tag and not a label, which would end a bundle and so
; could choose another template for the one that the call is in.

; ASM:      br.call.sptk.many b0 = g ;;
; ASM-NEXT: [.Ltmp{{[0-9]+}}:]
; ASM-NEXT: .loc
; ASM-NEXT: zxt4 r8 = r8

; OBJ-LABEL: <f>:
; OBJ:       1c: {{.*}} br.call.sptk.many b0 =
; OBJ-NEXT:  20: {{.*}} nop.m 0 // [MII]
; OBJ-NEXT:  26: {{.*}} zxt4 r8 = r8

; DWARF:      DW_TAG_call_site
; DWARF:        DW_AT_call_return_pc (0x0000000000000020)

define i64 @f(i32 %a) !dbg !8 {
entry:
  %call = tail call i32 @g(i32 %a), !dbg !13
  %conv = zext i32 %call to i64, !dbg !14
  ret i64 %conv, !dbg !15
}

declare !dbg !16 dso_local i32 @g(i32)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, isOptimized: true, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!8 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 2, type: !9, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0)
!9 = !DISubroutineType(types: !10)
!10 = !{!11, !12}
!11 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!12 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!13 = !DILocation(line: 2, column: 43, scope: !8)
!14 = !DILocation(line: 2, column: 33, scope: !8)
!15 = !DILocation(line: 2, column: 26, scope: !8)
!16 = !DISubprogram(name: "g", scope: !1, file: !1, line: 1, type: !17, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!17 = !DISubroutineType(types: !18)
!18 = !{!12, !12}
