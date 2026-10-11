// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d %t | FileCheck %s
// RUN: llvm-readobj -h %t | FileCheck %s --check-prefix=HDR

// Directives that say how the code is to be read, where the only way that
// there is to read it is the one they may say, and two that are for a
// checker of dependencies that is not here. The kernel begins with the
// first three.

	.psr abi64
	.psr lsb
	.lsb
	.PSR abi64, lsb
	.radix C
	.text
	.ln 7
	.serialize.data
	nop.m 0
	.serialize.instruction
	nop.m 1

// CHECK:      nop.m 0 // [MMI]
// CHECK-NEXT: nop.m 1
// CHECK-NEXT: nop.i 0 ;;

// HDR: Flags [ (0x10)
