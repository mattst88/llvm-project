// RUN: llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj %s -o %t.o
// RUN: llvm-readelf -s %t.o | FileCheck %s
// RUN: llvm-mc -triple ia64-unknown-linux-gnu %s -o %t.s
// RUN: FileCheck %s --check-prefix=ASM < %t.s
// RUN: llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj %t.s -o %t2.o
// RUN: cmp %t.o %t2.o
// RUN: not llvm-mc -triple ia64-unknown-linux-gnu --defsym ERR=1 %s \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

// A common symbol that does not say how it is aligned is aligned to its size,
// as far as 16 bytes, and .tls_common makes a thread-local one. GNU as wants
// .lcomm to say how its symbol is aligned, so none is written. .common is
// .comm by another name, and .lcomm does not make an object of its symbol as
// .local and .comm do. An alignment of 0 is one that was not said, which
// for .lcomm is none at all.

.ifndef ERR
	.comm	c1, 1
	.comm	c3, 3
	.comm	c5, 5
	.comm	c24, 24
	.comm	c100, 100, 32
	.common	k24, 24
	.common	k7, 7, 4
	.tls_common	t0, 0
	.tls_common	t4#, 4, 4
	.tls_common	t9, 9
	.local	l
	.comm	l, 24, 32
	.lcomm	m, 3, 8
	.comm	z8, 8, 0
	.comm	z100, 100, 0
	.tls_common	tz, 4, 0
	.lcomm	n, 100, 0
	.type	o, @object
	.lcomm	o, 8, 8

// ASM: .tls_common t4,4,4
// ASM: .local l
// ASM-NEXT: .comm l,24,32
// ASM-NEXT: .lcomm m,3,8
// ASM: .lcomm n,100,1
// ASM: .type o,@object
// ASM-NEXT: .lcomm o,8,8

// CHECK-DAG: 0000000000000000    24 OBJECT  LOCAL  DEFAULT [[#]] l
// CHECK-DAG: 0000000000000018     3 NOTYPE  LOCAL  DEFAULT [[#]] m
// CHECK-DAG: 0000000000000080     8 OBJECT  LOCAL  DEFAULT [[#]] o
// CHECK-DAG: 000000000000001b   100 NOTYPE  LOCAL  DEFAULT [[#]] n
// CHECK-DAG: 0000000000000008     8 OBJECT  GLOBAL DEFAULT   COM z8
// CHECK-DAG: 0000000000000010   100 OBJECT  GLOBAL DEFAULT   COM z100
// CHECK-DAG: 0000000000000004     4 TLS     GLOBAL DEFAULT   COM tz
// CHECK-DAG: 0000000000000001     1 OBJECT  GLOBAL DEFAULT   COM c1
// CHECK-DAG: 0000000000000004     3 OBJECT  GLOBAL DEFAULT   COM c3
// CHECK-DAG: 0000000000000008     5 OBJECT  GLOBAL DEFAULT   COM c5
// CHECK-DAG: 0000000000000010    24 OBJECT  GLOBAL DEFAULT   COM c24
// CHECK-DAG: 0000000000000020   100 OBJECT  GLOBAL DEFAULT   COM c100
// CHECK-DAG: 0000000000000010    24 OBJECT  GLOBAL DEFAULT   COM k24
// CHECK-DAG: 0000000000000004     7 OBJECT  GLOBAL DEFAULT   COM k7
// CHECK-DAG: 0000000000000001     0 TLS     GLOBAL DEFAULT   COM t0
// CHECK-DAG: 0000000000000004     4 TLS     GLOBAL DEFAULT   COM t4
// CHECK-DAG: 0000000000000010     9 TLS     GLOBAL DEFAULT   COM t9
.else
	.lcomm	e0, 4
// ERR: :[[#@LINE-1]]:14: error: expected alignment after size
	.tls_common	e1
// ERR: :[[#@LINE-1]]:16: error: expected comma
	.tls_common	e2, -1
// ERR: :[[#@LINE-1]]:18: error: size must be non-negative
	.tls_common	e3, 4, 3
// ERR: :[[#@LINE-1]]:21: error: alignment must be a power of 2
e4:
	.tls_common	e4, 4, 4
// ERR: :[[#@LINE-1]]:14: error: invalid symbol redefinition
.endif
