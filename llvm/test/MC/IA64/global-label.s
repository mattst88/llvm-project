// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -s %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// "name::" defines a symbol and makes it global, as it does for GNU as.

	.data
// ASM:      foo:
// ASM-NEXT: .globl foo
// ASM:      data1 0
foo::
	data1 0
// ASM:      bar:
// ASM-NEXT: .globl bar
// ASM:      data1 1
bar:: data1 1
local:
	data1 2
	.text
// ASM:      f:
// ASM-NEXT: .globl f
f::	nop 0

// CHECK-DAG: 0000000000000002 0 NOTYPE LOCAL  DEFAULT {{[0-9]+}} local
// CHECK-DAG: 0000000000000000 0 NOTYPE GLOBAL DEFAULT {{[0-9]+}} foo
// CHECK-DAG: 0000000000000001 0 NOTYPE GLOBAL DEFAULT {{[0-9]+}} bar
// CHECK-DAG: 0000000000000000 0 NOTYPE GLOBAL DEFAULT {{[0-9]+}} f
