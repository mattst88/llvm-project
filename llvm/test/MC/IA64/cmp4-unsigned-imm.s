// RUN: llvm-mc -triple ia64 -show-encoding %s | FileCheck %s
// RUN: not llvm-mc -triple ia64 --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR

// An unsigned compare of 32 bits takes its immediate written as 32 bits as
// well as sign-extended, as it does for GNU as: 0xffffffff is -1.

// CHECK: cmp4.ltu p6, p7 = -1, r4 // encoding: [0x80,0xe1,0x4f,0x38,0xbc,0x01]
	cmp4.ltu p6, p7 = 0xffffffff, r4
// CHECK: cmp4.ltu p6, p7 = -1, r4 // encoding: [0x80,0xe1,0x4f,0x38,0xbc,0x01]
	cmp4.ltu p6, p7 = -1, r4
// CHECK: cmp4.ltu p7, p6 = -128, r4 // encoding: [0xc0,0x01,0x40,0x30,0xbc,0x01]
	cmp4.geu p6, p7 = 0xffffff80, r4
// CHECK: cmp4.ltu p7, p6 = -128, r4 // encoding: [0xc0,0x01,0x40,0x30,0xbc,0x01]
	cmp4.gtu p6, p7 = 0xffffff81, r4
// CHECK: cmp4.ltu.unc p6, p7 = -2, r4 // encoding: [0x80,0xd1,0x4f,0x38,0xbc,0x01]
	CMP4.LEU.UNC p6, p7 = 0xffffffff, r4
// CHECK: (p3) cmp4.ltu p0, p6 = -16, r4 // encoding: [0x03,0x00,0x4e,0x30,0xbc,0x01]
	(p3) cmp4.geu p6 = 0xfffffff0, r4

.ifdef ERR
// Only as far as the 8 bits go.
// ERR: :[[#@LINE+1]]:20: error: immediate must be an integer in [-128, 127]
	cmp4.ltu p6, p7 = 0xffffff7f, r4
// ERR: :[[#@LINE+1]]:20: error: immediate must be a nonzero integer in [-127, 128]
	cmp4.gtu p6, p7 = 0xffffff80, r4
// A number of more than 32 bits is not one of 32 bits.
// ERR: :[[#@LINE+1]]:20: error: immediate must be an integer in [-128, 127]
	cmp4.ltu p6, p7 = 0x1ffffffff, r4
// The signed compare and the compare of 64 bits have no such reading.
// ERR: :[[#@LINE+1]]:19: error: immediate must be an integer in [-128, 127]
	cmp4.lt p6, p7 = 0xffffffff, r4
// ERR: :[[#@LINE+1]]:19: error: immediate must be an integer in [-128, 127]
	cmp.ltu p6, p7 = 0xffffffff, r4
.endif
