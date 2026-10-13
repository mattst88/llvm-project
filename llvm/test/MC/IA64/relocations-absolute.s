// RUN: llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj %s -o %t.o
// RUN: llvm-readobj -r --symbols %t.o | FileCheck %s

// A symbol that the file sets to a number further down is a symbol where
// it is used, and what a relocation specifier asks for is asked of the
// link editor with the symbol, which is kept for it. The GNU assembler does
// the same. A symbol that is already a number where it is used is the
// number, here as there, and fixups-invalid.s has what is said of that.

// CHECK:      .rela.text {
// CHECK-NEXT:   0x0 R_IA64_LTOFF22X abs 0x0
// CHECK-NEXT:   0x1 R_IA64_LTOFF22 abs 0x0
// CHECK-NEXT:   0x2 R_IA64_PLTOFF22 abs 0x0
// CHECK-NEXT: }
// CHECK-NEXT: .rela.data {
// CHECK-NEXT:   0x0 R_IA64_FPTR64LSB abs 0x0
// CHECK-NEXT:   0x8 R_IA64_LTOFF_FPTR64LSB abs 0x0
// CHECK-NEXT: }

// CHECK:      Name: abs
// CHECK-NEXT: Value: 0x1234
// CHECK-NEXT: Size: 0
// CHECK-NEXT: Binding: Local
// CHECK-NEXT: Type: None
// CHECK-NEXT: Other: 0
// CHECK-NEXT: Section: Absolute

	.text
	addl r14 = @ltoffx(abs), r1
	addl r14 = @ltoff(abs), r1
	addl r14 = @pltoff(abs), r1
	.data
	data8 @fptr(abs)
	data8 @ltoff(@fptr(abs))
abs = 0x1234
