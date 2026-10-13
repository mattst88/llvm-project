// A token that the lexer cannot make is reported where a directive of the
// target meets it, as it is where any other does. A string or a comment
// with no end runs to the end of the source, so each has a source to itself.

// RUN: echo '.long "' | not llvm-mc -triple ia64 -filetype=obj -o /dev/null \
// RUN:   2>&1 | FileCheck %s --check-prefix=LONG
// LONG: :1:7: error: unterminated string constant

// RUN: echo '.xdata4 .data, "' | not llvm-mc -triple ia64 -filetype=obj \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=XDATA
// XDATA: :1:16: error: unterminated string constant

// RUN: echo '.rotr a [ " ]' | not llvm-mc -triple ia64 -filetype=obj \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=ROTR
// ROTR: :1:11: error: unterminated string constant

// RUN: echo '.mem.offset /*' | not llvm-mc -triple ia64 -filetype=obj \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=COMMENT
// COMMENT: :1:13: error: unterminated comment

// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=error:

// CHECK: :[[#@LINE+1]]:10: error: single quote way too long
	.regstk ' 1, 2, 3
