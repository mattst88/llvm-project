; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s --check-prefix=CODE
; RUN: llvm-dwarfdump --debug-line %t.o | FileCheck %s

; A row of the line table is for an instruction, and three instructions share
; the sixteen bytes of a bundle, so the address in a row is that of the bundle
; with the number of the slot in its low bits, as it is for gas. The
; instructions of one bundle may be of different lines: the store of line 3
; is between the two halves of the call of line 4.

; CODE:      10: add r14 = r33, r32 ;;
; CODE-NEXT: 16: adds r33 = 28, r12
; CODE:      20: mov r37 = r33
; CODE-NEXT: 26: st4 [r33] = r14
; CODE-NEXT: 2c: br.call.sptk.many b0 =

; CHECK: min_inst_length: 1
; CHECK:      0x0000000000000000      2      0 {{.*}} is_stmt
; CHECK-NEXT: 0x0000000000000010      3     17 {{.*}} is_stmt prologue_end
; CHECK:      0x0000000000000020      4      3 {{.*}} is_stmt
; CHECK-NEXT: 0x0000000000000021      3      7 {{.*}} is_stmt
; CHECK-NEXT: 0x0000000000000022      4      3 {{.*}} is_stmt
; CHECK-NEXT: 0x0000000000000030      5     10 {{.*}} is_stmt
; CHECK:      0x0000000000000061      5      3 {{.*}} epilogue_begin
; CHECK-NEXT: 0x0000000000000080      5      3 {{.*}} end_sequence

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
