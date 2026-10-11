; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; The places that debug info names between two instructions are tags, which
; are the slots of the instructions after them, and so the bundles are what
; they are without debug info.

; CHECK-LABEL: f:
; CHECK:       add r14 = r32, r33 ;;
; CHECK-NEXT:  [.Ltmp0:]
; CHECK:       xor r8 = r14, r32
; CHECK-NEXT:  [.Ltmp1:]
; CHECK-NOT:   {{^}}.Ltmp

; OBJ:      add r14 = r32, r33 ;; // [MMI]
; OBJ-NEXT: xor r8 = r14, r32
; OBJ-NEXT: nop.i 0
; OBJ-NEXT: nop.m 0 // [MIB]
; OBJ-NEXT: nop.i 0
; OBJ-NEXT: br.ret.sptk.many b0 ;;
define i64 @f(i64 %a, i64 %b) !dbg !4 {
  %x = add i64 %a, %b, !dbg !12
    #dbg_value(i64 %x, !9, !DIExpression(), !12)
  %y = xor i64 %x, %a, !dbg !12
    #dbg_value(i64 %y, !9, !DIExpression(), !12)
  ret i64 %y, !dbg !12
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"Dwarf Version", i32 5}
!4 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 1, type: !5, unit: !0, retainedNodes: !8)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7, !7}
!7 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!8 = !{!9}
!9 = !DILocalVariable(name: "x", scope: !4, file: !1, line: 1, type: !7)
!12 = !DILocation(line: 1, scope: !4)
