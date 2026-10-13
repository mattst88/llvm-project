// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -s - | FileCheck %s
// RUN: not llvm-mc -triple ia64 -filetype=obj --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj \
// RUN:   | llvm-readelf -s - | FileCheck %s
// RUN: not llvm-mc -triple ia64 --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR

// A label can be given again where it already is, which is what a macro that
// begins a procedure does when its user has written the label too. Assembly
// has it as often as it was given.

// CHECK:      Symbol table '.symtab' contains 4 entries:
// CHECK:      0000000000000010  0 NOTYPE  LOCAL  DEFAULT [[#]] g
// CHECK-NEXT: 0000000000000000  0 NOTYPE  LOCAL  DEFAULT [[#]] d
// CHECK-NEXT: 0000000000000000 16 FUNC    GLOBAL DEFAULT [[#]] f
	.text
	.proc f
f:
	.global f
f:
	nop 0
	.endp f
g:
g:	nop 0
	.data
d:
d:	data8 1
.ifdef ERR
// ERR: :[[#@LINE+1]]:1: error: symbol 'd' is already defined
d:	data8 2
	.text
	nop 0 ;;
// ERR: :[[#@LINE+1]]:1: error: symbol 'g' is already defined
g:
.endif
