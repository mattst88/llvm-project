// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

// CHECK: :[[#@LINE+1]]:7: error: psr option 'abi32' is not supported
	.psr abi32
// CHECK: :[[#@LINE+1]]:12: error: psr option 'msb' is not supported
	.psr lsb, msb
// The options are in lower case.
// CHECK: :[[#@LINE+1]]:7: error: unknown psr option
	.psr ABI64
// CHECK: :[[#@LINE+1]]:6: error: expected a psr option
	.psr
// CHECK: :[[#@LINE+1]]:6: error: big-endian data is not supported
	.msb
// CHECK: :[[#@LINE+1]]:7: error: expected newline
	.lsb 1
// CHECK: :[[#@LINE+1]]:9: error: unsupported radix
	.radix 16
// CHECK: :[[#@LINE+1]]:9: error: unsupported radix
	.radix x
// CHECK: :[[#@LINE+1]]:18: error: expected newline
	.serialize.data 3
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.serialize.bogus
// CHECK: :[[#@LINE+1]]:6: error: expected absolute expression
	.ln foo
