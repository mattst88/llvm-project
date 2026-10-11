// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	.text
// CHECK: :[[#@LINE+1]]:3: error: expected ':'
[a]	nop 0
// CHECK: :[[#@LINE+1]]:5: error: expected ']'
[a:	nop 0
// CHECK: :[[#@LINE+1]]:2: error: expected a symbol
[(p6):]	nop 0
// Only an instruction or data can follow on the line.
// CHECK: :[[#@LINE+1]]:6: error: expected an instruction after the tag
[a:] .mii
// CHECK: :[[#@LINE+1]]:6: error: expected an instruction after the tag
[a:] 5
// CHECK: :[[#@LINE+1]]:11: error: expected an instruction after the tag
[a:] [b:] ]

// A tag defines its symbol.
[c:]	nop 0
// CHECK: :[[#@LINE+1]]:2: error: symbol 'c' is already defined
[c:]	nop 0
[d:]
// CHECK: :[[#@LINE+1]]:2: error: symbol 'd' is already defined
[d:]	nop 0
e:
// CHECK: :[[#@LINE+1]]:2: error: symbol 'e' is already defined
[e:]	nop 0
[f:]	nop 0
// CHECK: :[[#@LINE+1]]:1: error: symbol 'f' is already defined
f:
