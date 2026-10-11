// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r %t | FileCheck %s
// RUN: llvm-readelf -s %t | FileCheck %s --check-prefix=SYM

// A relocation against a local symbol is against its section, as GNU as has
// it, unless it is for a function descriptor or for a thread-local variable.

// SYM-DAG: TLS LOCAL DEFAULT {{.*}} .Ltl
// SYM-DAG: TLS LOCAL DEFAULT {{.*}} tl

	.text
	.p2align 4
f:
// CHECK:      Section (3) .rela.text {
// CHECK-NEXT:   0x0 R_IA64_GPREL22 .data 0x8
	addl r14 = @gprel(.Ld), r1
// CHECK-NEXT:   0x1 R_IA64_LTOFF22 .data 0x8
	addl r14 = @ltoff(.Ld), r1
// CHECK-NEXT:   0x2 R_IA64_LTOFF22 .data 0x18
	addl r14 = @ltoff(st+8), r1
// CHECK-NEXT:   0x11 R_IA64_IMM64 .data 0x8
	movl r14 = .Ld
// CHECK-NEXT:   0x21 R_IA64_IMM64 .text 0x60
	movl r14 = .Lt
// A function descriptor is for a function, not for an offset in a section.
// CHECK-NEXT:   0x30 R_IA64_LTOFF_FPTR22 f 0x0
	addl r14 = @ltoff(@fptr(f)), r1
// CHECK-NEXT:   0x31 R_IA64_LTOFF_FPTR64I f 0x0
	movl r14 = @ltoff(@fptr(f))
// CHECK-NEXT:   0x41 R_IA64_FPTR64I f 0x0
	movl r14 = @fptr(f)
// So is a thread-local variable, and the specifier makes the symbol one.
// CHECK-NEXT:   0x50 R_IA64_TPREL14 .Ltl 0x0
	adds r14 = @tprel(.Ltl), r13
// CHECK-NEXT:   0x51 R_IA64_LTOFF_DTPMOD22 tl 0x0
	addl r14 = @ltoff(@dtpmod(tl)), r1
.Lt:
// A branch to another section is against the section.
// CHECK-NEXT:   0x61 R_IA64_PCREL21B .text.h 0x0
	br.call.sptk.many b0 = h
// CHECK-NEXT:   0x62 R_IA64_PCREL21B .text.h 0x10
	br.call.sptk.many b0 = .Lh

// CHECK-NEXT: }
	.section .text.h,"ax",@progbits
	.p2align 4
h:
	nop.i 0
.Lh:
	nop.i 0

	.data
	.8byte 0
.Ld:
// CHECK:      Section (6) .rela.data {
// CHECK-NEXT:   0x8 R_IA64_DIR64LSB .data 0x8
	.8byte .Ld
st:
// CHECK-NEXT:   0x10 R_IA64_DIR64LSB .data 0x10
	.8byte st
// CHECK-NEXT:   0x18 R_IA64_DIR64LSB .text 0x60
	.8byte .Lt
// CHECK-NEXT:   0x20 R_IA64_GPREL64LSB .data 0x10
	.8byte @gprel(st)
// CHECK-NEXT:   0x28 R_IA64_PCREL64LSB .text 0x60
	.8byte @pcrel(.Lt)
// CHECK-NEXT:   0x30 R_IA64_SECREL64LSB .data 0x8
	.8byte @secrel(.Ld)
// CHECK-NEXT:   0x38 R_IA64_SEGREL64LSB .data 0x10
	.8byte @segrel(st)
// CHECK-NEXT:   0x40 R_IA64_FPTR64LSB f 0x0
	.8byte @fptr(f)
// CHECK-NEXT:   0x48 R_IA64_FPTR32LSB f 0x0
	.4byte @fptr(f)
// In data the linkage table entry of a descriptor is against the section.
// CHECK-NEXT:   0x4C R_IA64_LTOFF_FPTR64LSB .text 0x0
	.8byte @ltoff(@fptr(f))
// CHECK-NEXT:   0x54 R_IA64_LTOFF_FPTR32LSB .text 0x0
	.4byte @ltoff(@fptr(f))
// CHECK-NEXT:   0x58 R_IA64_DTPREL64LSB tl 0x0
	.8byte @dtprel(tl)

// CHECK-NEXT: }
	.section .tdata,"awT",@progbits
	.8byte 0
.Ltl:
	.8byte 0
tl:
	.8byte 0
