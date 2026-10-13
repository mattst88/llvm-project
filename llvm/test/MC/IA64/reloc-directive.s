// RUN: llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj %s -o %t.o
// RUN: llvm-readobj -r %t.o | FileCheck %s
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS
// RUN: not llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj \
// RUN:   --defsym ERR=1 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

// .reloc names a relocation by what follows "R_IA64_", which is the name
// the GNU assembler has for it, or by the whole of its name. The place is
// an address of a slot for one that is applied to an instruction, and
// nothing is written into the place. BFD_RELOC_NONE is the only generic name.

// CHECK:      .rela.text {
// CHECK-NEXT:   0x0 R_IA64_LDXMOV sym 0x0
// CHECK-NEXT:   0x0 R_IA64_NONE sym 0x0
// CHECK-NEXT:   0x0 R_IA64_NONE sym 0x0
// CHECK-NEXT:   0x1 R_IA64_LTOFF22X sym 0x8
// CHECK-NEXT:   0x2 R_IA64_PCREL21B f 0x0
// CHECK-NEXT: }
// CHECK-NEXT: .rela.data {
// CHECK-NEXT:   0x0 R_IA64_DIR64LSB sym 0x0
// CHECK-NEXT:   0x0 R_IA64_FPTR64LSB f 0x0
// CHECK-NEXT:   0x8 R_IA64_SECREL32MSB d 0x8
// CHECK-NEXT: }

// DIS:      ld8 r14 = [r14]
// DIS-NEXT: addl r2 = 0, r1
// DIS-NEXT: br.call.sptk.many b0 = 0x0

	.text
f:
	.reloc ., LDXMOV, sym
	.reloc f, R_IA64_NONE, sym
	.reloc f, BFD_RELOC_NONE, sym
	.reloc . + 1, LTOFF22X, sym + 8
	.reloc . + 2, R_IA64_PCREL21B, f
	ld8 r14 = [r14]
	addl r2 = 0, r1
	br.call.sptk.many b0 = 0
	.data
d:
	.reloc ., DIR64LSB, sym
	.reloc ., R_IA64_FPTR64LSB, f
	.reloc d + 8, SECREL32MSB, d + 8
	data8 0
	data8 0

.ifdef ERR
// ERR: :[[#@LINE+1]]:12: error: unknown relocation name
	.reloc ., R_IA64_NOSUCH, sym
// ERR: :[[#@LINE+1]]:12: error: unknown relocation name
	.reloc ., R_X86_64_NONE, sym
// ERR: :[[#@LINE+1]]:12: error: unknown relocation name
	.reloc ., BFD_RELOC_64, sym
.endif
