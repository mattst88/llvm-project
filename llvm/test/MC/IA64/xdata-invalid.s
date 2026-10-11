// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

	.text
// CHECK: :[[#@LINE+1]]:10: error: expected a section name
	.xdata4 1, 2
// CHECK: :[[#@LINE+1]]:15: error: expected comma
	.xdata4 .data
// CHECK: :[[#@LINE+1]]:18: error: expected string
	.xstring .data, 1
// Only the directives that have one take ".ua".
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.xdata1.ua .data, 1
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.xstring.ua .data, "a"
