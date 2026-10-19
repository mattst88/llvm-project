// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

// llvm-objdump skips runs of zero bytes, but IA-64 instructions are not
// byte aligned, and nop.x is mostly zeros. Only whole bundles are skipped.

// CHECK:      0: nop.m 0 // [MLX]
// CHECK-NEXT: 6: nop.x 0 ;;
// CHECK-NEXT: ...
// CHECK-NEXT: 30: nop.m 0 // [MLX]
// CHECK-NEXT: 36: nop.x 0

	.text
	{ .mlx
	nop.m 0
	nop.x 0 ;;
	}
	.zero 32
	{ .mlx
	nop.m 0
	nop.x 0
	}
