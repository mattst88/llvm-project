// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -dr --no-show-raw-insn - | FileCheck %s
// RUN: llvm-mc -triple ia64 -show-encoding %s | FileCheck %s --check-prefix=ENC

// A reference to a place in the same section needs no relocation. The
// displacement of a branch is from the bundle the branch is in to the bundle
// of the target, whichever slot the branch is in. A tag is the distance to the
// branch it is about and is never relocated.

// CHECK-NOT: R_IA64
// CHECK:      0000000000000000 <f>:
// CHECK-NEXT: 0: nop.m 0 // [MII]
// CHECK-NEXT: 6: nop.i 0
// CHECK-NEXT: c: nop.i 0
// CHECK-NEXT: 10: br.call.sptk.many b0 = 0x80 // [BBB]
// CHECK-NEXT: 16: br.cond.dptk.few 0x10
// CHECK-NEXT: 1c: br.cloop.sptk.few 0x0
// CHECK-NEXT: 20: chk.a.nc r4, 0x80 // [MMI]
// CHECK-NEXT: 26: chk.s.m r4, 0x80
// CHECK-NEXT: 2c: chk.s.i r4, 0x0
// CHECK-NEXT: 30: nop.m 0 // [MFI]
// CHECK-NEXT: 36: fchkf 0x80
// CHECK-NEXT: 3c: nop.i 0
// CHECK-NEXT: 40: nop.m 0 // [MLX]
// CHECK-NEXT: 46: brl.call.sptk.many b0 = 0x80
// CHECK-NEXT: 50: nop.m 0 // [MLX]
// CHECK-NEXT: 56: brl.cond.sptk.many 0x0
// CHECK-NEXT: 60: nop.m 0 // [MBB]
// CHECK-NEXT: 66: brp.sptk 0x80, 0x80
// CHECK-NEXT: 6c: brp.ret.sptk.imp b0, 0x10
// CHECK-NEXT: 70: nop.m 0 // [MII]
// CHECK-NEXT: 76: mov b6 = r2, 0x80
// CHECK-NEXT: 7c: mov.ret b6 = r2, 0x10
// CHECK-NEXT: 80: adds r4 = 128, r5 // [MII]
// CHECK-NEXT: 86: adds r4 = -128, r5
// CHECK-NEXT: 8c: addl r4 = 128, r1
// CHECK-NEXT: 90: addl r4 = -128, r1 // [MLX]
// CHECK-NEXT: 96: movl r4 = 128
// CHECK-NEXT: a0: nop.m 0 // [MLX]
// CHECK-NEXT: a6: movl r4 = -128
// CHECK-NEXT: b0: addl r4 = 128, r1 // [MII]
// CHECK-NEXT: b6: adds r4 = 128, r5
// CHECK-NEXT: bc: mov r4 = 128 ;;

// ENC:      br.call.sptk.many b0 = .Lb // encoding: [0x00,0bAAA10000,A,A,0b1010000A,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_pcrel21b
// ENC:      chk.s.m r4, .Lb // encoding: [0x00,0x80,0bAAAA0000,A,0b0010001A,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_pcrel21m
// ENC:      fchkf .Lb // encoding: [0bAA000000,A,A,0b010000AA,0x00,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_pcrel21f
// ENC:      brl.call.sptk.many b0 = .Lb // encoding: [0bAAAAAA00,A,A,A,A,0b0000000A,0x00,0x10,0x00,0x00,0xa0,0x01]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_pcrel60b
// ENC:      brp.sptk .Lb, .Lb // encoding: [0bBB000000,0bAAABBBBB,A,A,0b1110000A,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_pcrel21b
// ENC-NEXT: // fixup B - offset: 0, value: .Lb, kind: fixup_ia64_tag13b
// ENC:      mov b6 = r2, .Lb // encoding: [0x80,0x41,0x10,A,0b0000111A,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb, kind: fixup_ia64_tag13i
// ENC:      adds r4 = .Lb-f, r5 // encoding: [0x00,0bAAA00001,0b0101AAAA,0x00,0x08,0x01]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb-f, kind: fixup_ia64_imm14
// ENC:      addl r4 = .Lb-f, r1 // encoding: [0x00,0x01,0x10,0bAAAAA000,0b0010AAAA,0x01]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb-f, kind: fixup_ia64_imm22
// ENC:      movl r4 = .Lb-f // encoding: [A,A,A,A,A,0b0000000A,0x00,0x01,0x00,0x00,0xc0,0x00]
// ENC-NEXT: // fixup A - offset: 0, value: .Lb-f, kind: fixup_ia64_imm64

	.text
f:
	nop.i 0
.La:
	br.call.sptk.many b0 = .Lb
	br.cond.dptk.few .La
	br.cloop.sptk.few f
	chk.a.nc r4, .Lb
	chk.s.m r4, .Lb
	chk.s.i r4, f
	fchkf.s0 .Lb
	brl.call.sptk.many b0 = .Lb
	brl.cond.sptk.many f
	brp.sptk .Lb, .Lb
	brp.ret.sptk.imp b0, .La
	mov b6 = r2, .Lb
	mov.ret b6 = r2, .La
.Lb:
	adds r4 = .Lb - f, r5
	adds r4 = f - .Lb, r5
	addl r4 = .Lb - f, r1
	addl r4 = f - .Lb, r1
	movl r4 = .Lb - f
	movl r4 = f - .Lb
	add r4 = .Lb - f, r1
	add r4 = .Lb - f, r5
	mov r4 = .Lb - f
