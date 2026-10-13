; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-dwarfdump --debug-info %t.o 2>&1 | FileCheck %s --check-prefix=DWARF

; Where a thread-local variable is, for a debugger, is its offset in the
; thread-local storage of its module, as it is for gcc: a link editor that
; is given the address of the variable makes of it the address in the image
; that the variable is initialized from.

; ASM:      data1 10 // DW_AT_location
; ASM-NEXT: data1 14
; ASM-NEXT: data8.ua @dtprel(tv)
; ASM-NEXT: data1 224

; RELOC:      Section ({{.*}}) .rela.debug_info {
; RELOC:        R_IA64_DTPREL64LSB tv 0x0
; RELOC-NOT:    R_IA64_DIR64LSB tv

; The offset is read from the object as any other word of the debugging
; information is.

; DWARF-NOT: failed to compute relocation
; DWARF:     DW_AT_name ("tv")
; DWARF:     DW_AT_location (DW_OP_const8u 0x0, DW_OP_GNU_push_tls_address)
; DWARF-NOT: failed to compute relocation

@tv = dso_local thread_local global i32 5, align 4, !dbg !0

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!6, !7}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "tv", scope: !2, file: !3, line: 1, type: !5, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !4, splitDebugInlining: false, nameTableKind: None)
!3 = !DIFile(filename: "t.c", directory: "/")
!4 = !{!0}
!5 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!6 = !{i32 7, !"Dwarf Version", i32 4}
!7 = !{i32 2, !"Debug Info Version", i32 3}
