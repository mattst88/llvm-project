// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -d %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t2
// RUN: cmp %t %t2

// The instructions between "{" and "}" are one bundle, of the template that
// a directive names or of one that they fit. A brace is not a stop: the
// bundle before the "{" ends without one and so does the one at the "}".

	.text
f:
{ .mii
	nop.m 1
	nop.i 2
	nop.i 3
}
// ASM:      {
// ASM-NEXT: .mii
// ASM-NEXT: nop.m	1
// ASM-NEXT: nop.i	2
// ASM-NEXT: nop.i	3
// ASM-NEXT: }
// CHECK:      nop.m 1 // [MII]
// CHECK-NEXT: nop.i 2
// CHECK-NEXT: nop.i 3{{$}}

// The instruction before a brace is in a bundle of its own.
	nop.i 4
// CHECK-NEXT: nop.m 0 // [MII]
// CHECK-NEXT: nop.i 4
// CHECK-NEXT: nop.i 0{{$}}

// A stop inside is one of the template.
{
	nop.m 5 ;;
	nop.m 6
}
// CHECK-NEXT: nop.m 5 ;; // [MMI]
// CHECK-NEXT: nop.m 6
// CHECK-NEXT: nop.i 0{{$}}

// What does not fill the bundle leaves the rest to "nop".
{
	nop.i 7
}
	nop.i 8
// CHECK-NEXT: nop.m 0 // [MII]
// CHECK-NEXT: nop.i 7
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MII]
// CHECK-NEXT: nop.i 8
// CHECK-NEXT: nop.i 0{{$}}

// A brace may share a line with what is next to it.
{ nop.m 9
  nop.f 10 }
// ASM:      nop.i	8
// ASM-EMPTY:
// ASM-NEXT: {
// ASM-NEXT: nop.m	9
// ASM-NEXT: nop.f	10
// ASM-NEXT: }
// CHECK-NEXT: nop.m 9 // [MFI]
// CHECK-NEXT: nop.f 10
// CHECK-NEXT: nop.i 0{{$}}

// A label may come first, and two words are one instruction.
{
g:	nop.m 11
	movl r4 = 5
}
// CHECK:      <g>:
// CHECK-NEXT: nop.m 11 // [MLX]
// CHECK-NEXT: movl r4 = 5 ;;
