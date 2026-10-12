; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-dwarfdump --debug-info - | FileCheck %s

; What a call site says is called is the second operand of the call: the
; first is b0, which the return address goes to. A call of a function names
; the function, and a call through a pointer names b6, which the entry point
; was put in and which the callee is free to change.

; CHECK:      DW_AT_name ("direct")
; CHECK:      DW_TAG_call_site
; CHECK-NEXT:   DW_AT_call_origin ({{.*}} "g")
; CHECK-NEXT:   DW_AT_call_return_pc

; CHECK:      DW_AT_name ("indirect")
; CHECK:      DW_TAG_call_site
; CHECK-NEXT:   DW_AT_call_target_clobbered (DW_OP_regx B6)
; CHECK-NEXT:   DW_AT_call_return_pc

define i32 @direct(i32 %a) !dbg !8 {
entry:
  %call = call i32 @g(i32 %a), !dbg !13
  %add = add i32 %call, 1, !dbg !13
  ret i32 %add, !dbg !13
}

define i32 @indirect(ptr %p, i32 %a) !dbg !14 {
entry:
  %call = call i32 %p(i32 %a), !dbg !18
  %add = add i32 %call, 1, !dbg !18
  ret i32 %add, !dbg !18
}

declare !dbg !19 i32 @g(i32)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, isOptimized: true, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!8 = distinct !DISubprogram(name: "direct", scope: !1, file: !1, line: 2, type: !9, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0)
!9 = !DISubroutineType(types: !10)
!10 = !{!11, !11}
!11 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!13 = !DILocation(line: 2, column: 3, scope: !8)
!14 = distinct !DISubprogram(name: "indirect", scope: !1, file: !1, line: 3, type: !15, scopeLine: 3, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0)
!15 = !DISubroutineType(types: !16)
!16 = !{!11, !17, !11}
!17 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !9, size: 64)
!18 = !DILocation(line: 3, column: 3, scope: !14)
!19 = !DISubprogram(name: "g", scope: !1, file: !1, line: 1, type: !9, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
