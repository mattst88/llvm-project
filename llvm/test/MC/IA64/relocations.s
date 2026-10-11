// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readobj -r - | FileCheck %s

// What a relocation specifier asks for depends on the field it is in. A
// relocation in an instruction is at the address of the bundle plus the number
// of the slot. GNU as writes the same relocations for this file.

	.text
	.p2align 4
f:
// 22-bit immediates.
// CHECK:      Section (3) .rela.text {
// CHECK-NEXT:   0x0 R_IA64_IMM22 x 0x0
	addl r14 = x, r1
// CHECK-NEXT:   0x1 R_IA64_GPREL22 x 0x0
	addl r14 = @gprel(x), r1
// CHECK-NEXT:   0x2 R_IA64_LTOFF22 x 0x0
	addl r14 = @ltoff(x), r1
// CHECK-NEXT:   0x10 R_IA64_LTOFF22X x 0x0
	addl r14 = @ltoffx(x), r1
// CHECK-NEXT:   0x11 R_IA64_PLTOFF22 x 0x0
	addl r14 = @pltoff(x), r1
// CHECK-NEXT:   0x12 R_IA64_PCREL22 x 0x0
	addl r14 = @pcrel(x), r1
// CHECK-NEXT:   0x20 R_IA64_TPREL22 t 0x0
	addl r14 = @tprel(t), r1
// CHECK-NEXT:   0x21 R_IA64_DTPREL22 t 0x0
	addl r14 = @dtprel(t), r2
// CHECK-NEXT:   0x22 R_IA64_LTOFF_FPTR22 x 0x0
	addl r14 = @ltoff(@fptr(x)), r1
// CHECK-NEXT:   0x30 R_IA64_LTOFF_TPREL22 t 0x0
	addl r14 = @ltoff(@tprel(t)), r1
// CHECK-NEXT:   0x31 R_IA64_LTOFF_DTPMOD22 t 0x0
	addl r14 = @ltoff(@dtpmod(t)), r1
// CHECK-NEXT:   0x32 R_IA64_LTOFF_DTPREL22 t 0x0
	addl r14 = @ltoff(@dtprel(t)), r1
// CHECK-NEXT:   0x40 R_IA64_IMM22 x 0x18
	addl r14 = x+24, r1
// CHECK-NEXT:   0x41 R_IA64_GPREL22 x 0x18
	addl r14 = @gprel(x+24), r1

// 14-bit immediates.
// CHECK-NEXT:   0x42 R_IA64_IMM14 x 0x0
	adds r14 = x, r4
// CHECK-NEXT:   0x50 R_IA64_TPREL14 t 0x0
	adds r14 = @tprel(t), r13
// CHECK-NEXT:   0x51 R_IA64_DTPREL14 t 0x0
	adds r14 = @dtprel(t), r8

// 64-bit immediates. The relocation is at the L slot.
// CHECK-NEXT:   0x61 R_IA64_IMM64 x 0x0
	movl r14 = x
// CHECK-NEXT:   0x71 R_IA64_GPREL64I x 0x0
	movl r14 = @gprel(x)
// CHECK-NEXT:   0x81 R_IA64_LTOFF64I x 0x0
	movl r14 = @ltoff(x)
// CHECK-NEXT:   0x91 R_IA64_PLTOFF64I x 0x0
	movl r14 = @pltoff(x)
// CHECK-NEXT:   0xA1 R_IA64_PCREL64I x 0x0
	movl r14 = @pcrel(x)
// CHECK-NEXT:   0xB1 R_IA64_FPTR64I x 0x0
	movl r14 = @fptr(x)
// CHECK-NEXT:   0xC1 R_IA64_TPREL64I t 0x0
	movl r14 = @tprel(t)
// CHECK-NEXT:   0xD1 R_IA64_DTPREL64I t 0x0
	movl r14 = @dtprel(t)
// CHECK-NEXT:   0xE1 R_IA64_LTOFF_FPTR64I x 0x0
	movl r14 = @ltoff(@fptr(x))
// CHECK-NEXT:   0xF1 R_IA64_IMM64 x 0xFFFFFFFFFFFFFFF8
	movl r14 = x-8

// Branches.
// CHECK-NEXT:   0x100 R_IA64_PCREL21B x 0x0
	br.call.sptk.many b0 = x
// CHECK-NEXT:   0x101 R_IA64_PCREL21B x 0x20
	br.cond.dptk.few x+32
// CHECK-NEXT:   0x102 R_IA64_PCREL21B x 0x0
	br.cloop.sptk.few x
// CHECK-NEXT:   0x112 R_IA64_PCREL21B x 0x0
	brp.sptk x, .Ltag
.Ltag:
// CHECK-NEXT:   0x120 R_IA64_PCREL21B x 0x0
	chk.a.nc r4, x
// CHECK-NEXT:   0x121 R_IA64_PCREL21M x 0x0
	chk.s.m r4, x
// CHECK-NEXT:   0x122 R_IA64_PCREL21M x 0x0
	chk.s.i r4, x
// CHECK-NEXT:   0x131 R_IA64_PCREL21F x 0x0
	fchkf.s0 x
// CHECK-NEXT:   0x141 R_IA64_PCREL60B x 0x0
	brl.call.sptk.many b0 = x
// CHECK-NEXT:   0x151 R_IA64_PCREL60B x 0x10
	brl.cond.sptk.many x+16

// Data.
// CHECK-NEXT: }
	.data
d:
// CHECK:      Section (5) .rela.data {
// CHECK-NEXT:   0x0 R_IA64_DIR64LSB x 0x0
	.8byte x
// CHECK-NEXT:   0x8 R_IA64_DIR64LSB x 0x8
	.8byte x+8
// CHECK-NEXT:   0x10 R_IA64_GPREL64LSB x 0x0
	.8byte @gprel(x)
// CHECK-NEXT:   0x18 R_IA64_PLTOFF64LSB x 0x0
	.8byte @pltoff(x)
// CHECK-NEXT:   0x20 R_IA64_PCREL64LSB x 0x0
	.8byte @pcrel(x)
// CHECK-NEXT:   0x28 R_IA64_FPTR64LSB x 0x0
	.8byte @fptr(x)
// CHECK-NEXT:   0x30 R_IA64_LTOFF_FPTR64LSB x 0x0
	.8byte @ltoff(@fptr(x))
// CHECK-NEXT:   0x38 R_IA64_SECREL64LSB x 0x0
	.8byte @secrel(x)
// CHECK-NEXT:   0x40 R_IA64_SEGREL64LSB x 0x0
	.8byte @segrel(x)
// CHECK-NEXT:   0x48 R_IA64_LTV64LSB x 0x0
	.8byte @ltv(x)
// CHECK-NEXT:   0x50 R_IA64_TPREL64LSB t 0x0
	.8byte @tprel(t)
// CHECK-NEXT:   0x58 R_IA64_DTPMOD64LSB t 0x0
	.8byte @dtpmod(t)
// CHECK-NEXT:   0x60 R_IA64_DTPREL64LSB t 0x0
	.8byte @dtprel(t)
// CHECK-NEXT:   0x68 R_IA64_PCREL64LSB x 0x0
	.8byte x - .
// CHECK-NEXT:   0x70 R_IA64_DIR32LSB x 0x0
	.4byte x
// CHECK-NEXT:   0x74 R_IA64_GPREL32LSB x 0x0
	.4byte @gprel(x)
// CHECK-NEXT:   0x78 R_IA64_PCREL32LSB x 0x0
	.4byte @pcrel(x)
// CHECK-NEXT:   0x7C R_IA64_FPTR32LSB x 0x0
	.4byte @fptr(x)
// CHECK-NEXT:   0x80 R_IA64_LTOFF_FPTR32LSB x 0x0
	.4byte @ltoff(@fptr(x))
// CHECK-NEXT:   0x84 R_IA64_SECREL32LSB x 0x0
	.4byte @secrel(x)
// CHECK-NEXT:   0x88 R_IA64_SEGREL32LSB x 0x0
	.4byte @segrel(x)
// CHECK-NEXT:   0x8C R_IA64_LTV32LSB x 0x0
	.4byte @ltv(x)
// CHECK-NEXT:   0x90 R_IA64_DTPREL32LSB t 0x0
	.4byte @dtprel(t)
// CHECK-NEXT:   0x94 R_IA64_PCREL32LSB x 0x0
	.4byte x - .
// CHECK-NEXT: }
