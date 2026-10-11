// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s

// A region begins right after the instruction before it, and has the slots
// that are left empty between that one and its own first instruction. The
// slots that a bundle in braces leaves empty are that bundle's, though: a
// region that begins after the closing brace begins with the next bundle.
// GNU as writes the same object for this file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// The second body is the last bundle, of three slots.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01250023 00000000
	.proc f1
f1:
	.prologue
	mov r2 = r3
	.body
	mov r4 = r5 ;;
	{ .mii
	mov r6 = r7 ;;
	}
	.prologue
	.body
	mov r8 = r9 ;;
	.endp f1

// Here it has the two empty slots before that bundle as well.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01230025 00000000
	.proc f2
f2:
	.prologue
	mov r2 = r3
	.body
	mov r4 = r5 ;;
	mov r6 = r7 ;;
	mov r10 = r11 ;;
	{ .mii
	.prologue
	.body
	mov r8 = r9 ;;
	}
	.endp f2
