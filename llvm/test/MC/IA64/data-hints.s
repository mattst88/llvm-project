// The data access hints of the later processors: the completers .d1 to .d7
// on an access that leaves its base alone and .d0 to .d7 on a line prefetch,
// the counted prefetch, and the moves to and from the data access hint
// registers. The sixteen-byte accesses and the compare and exchanges may also
// leave out the application registers they imply.
// The encodings are those GNU as produces.

// RUN: llvm-mc -triple ia64 -show-encoding %s | FileCheck %s
// RUN: llvm-mc -triple ia64 -filetype=obj %s \
// RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=DIS

// CHECK: ld8.nt2 r4 = [r5] // encoding: [0x00,0x01,0x50,0xe0,0x80,0x00]
	ld8.nt2 r4 = [r5]
// CHECK: ld8.nt1 r4 = [r5] // encoding: [0x00,0x01,0x50,0xd0,0x80,0x00]
	ld8.d1 r4 = [r5]
// CHECK: ld8.nt2 r4 = [r5] // encoding: [0x00,0x01,0x50,0xe0,0x80,0x00]
	ld8.d2 r4 = [r5]
// CHECK: ld8.nta r4 = [r5] // encoding: [0x00,0x01,0x50,0xf0,0x80,0x00]
	ld8.d3 r4 = [r5]
// CHECK: ld8.d4 r4 = [r5] // encoding: [0x00,0x01,0x58,0xc0,0x80,0x00]
	ld8.d4 r4 = [r5]
// CHECK: ld1.s.d5 r4 = [r5] // encoding: [0x00,0x01,0x58,0x10,0x81,0x00]
	ld1.s.d5 r4 = [r5]
// CHECK: ld2.c.clr.d6 r4 = [r5] // encoding: [0x00,0x01,0x58,0x60,0x88,0x00]
	ld2.c.clr.d6 r4 = [r5]
// CHECK: ld8.fill.d7 r4 = [r5] // encoding: [0x00,0x01,0x58,0xf0,0x86,0x00]
	ld8.fill.d7 r4 = [r5]
// CHECK: ld16.d4 r4, ar.csd = [r5] // encoding: [0x00,0x01,0x58,0x08,0x8a,0x00]
	ld16.d4 r4, ar.csd = [r5]
// CHECK: ld16.acq.d7 r4, ar.csd = [r5] // encoding: [0x00,0x01,0x58,0x38,0x8b,0x00]
	ld16.acq.d7 r4, ar.csd = [r5]
// CHECK: ldfd.d4 f6 = [r5] // encoding: [0x80,0x01,0x58,0xc0,0xc0,0x00]
	ldfd.d4 f6 = [r5]
// CHECK: ldfe.sa.d5 f6 = [r5] // encoding: [0x80,0x01,0x58,0x10,0xc3,0x00]
	ldfe.sa.d5 f6 = [r5]
// CHECK: ldf.fill.d7 f6 = [r5] // encoding: [0x80,0x01,0x58,0xf0,0xc6,0x00]
	ldf.fill.d7 f6 = [r5]
// CHECK: st8.nt1 [r5] = r4 // encoding: [0x00,0x80,0x50,0xd0,0x8c,0x00]
	st8.nt1 [r5] = r4
// CHECK: st8.nt2 [r5] = r4 // encoding: [0x00,0x80,0x50,0xe0,0x8c,0x00]
	st8.nt2 [r5] = r4
// CHECK: st8.nta [r5] = r4 // encoding: [0x00,0x80,0x50,0xf0,0x8c,0x00]
	st8.d3 [r5] = r4
// CHECK: st8.d4 [r5] = r4 // encoding: [0x00,0x90,0x50,0xc0,0x8c,0x00]
	st8.d4 [r5] = r4
// CHECK: st4.rel.d5 [r5] = r4 // encoding: [0x00,0x90,0x50,0x90,0x8d,0x00]
	st4.rel.d5 [r5] = r4
// CHECK: st8.spill.d6 [r5] = r4 // encoding: [0x00,0x90,0x50,0xe0,0x8e,0x00]
	st8.spill.d6 [r5] = r4
// CHECK: st16.d7 [r5] = r4, ar.csd // encoding: [0x00,0x90,0x50,0x38,0x8c,0x00]
	st16.d7 [r5] = r4, ar.csd
// CHECK: st16.rel.d4 [r5] = r4, ar.csd // encoding: [0x00,0x90,0x50,0x08,0x8d,0x00]
	st16.rel.d4 [r5] = r4, ar.csd
// CHECK: stfd.d4 [r5] = f6 // encoding: [0x00,0xd0,0x50,0xc0,0xcc,0x00]
	stfd.d4 [r5] = f6
// CHECK: stf.spill.d7 [r5] = f6 // encoding: [0x00,0xd0,0x50,0xf0,0xce,0x00]
	stf.spill.d7 [r5] = f6
// CHECK: lfetch [r5] // encoding: [0x00,0x00,0x50,0x00,0xcb,0x00]
	lfetch.d0 [r5]
// CHECK: lfetch.nt1 [r5] // encoding: [0x00,0x00,0x50,0x10,0xcb,0x00]
	lfetch.d1 [r5]
// CHECK: lfetch.d4 [r5] // encoding: [0x00,0x10,0x50,0x00,0xcb,0x00]
	lfetch.d4 [r5]
// CHECK: lfetch.excl.d5 [r5], r6 // encoding: [0x00,0xd0,0x50,0x50,0xdb,0x00]
	lfetch.excl.d5 [r5], r6
// CHECK: lfetch.fault.d6 [r5], 8 // encoding: [0x00,0x10,0x51,0xa0,0xeb,0x00]
	lfetch.fault.d6 [r5], 8
// CHECK: lfetch.fault.excl.d7 [r5], -256 // encoding: [0x00,0x10,0x50,0xf0,0xfb,0x00]
	lfetch.fault.excl.d7 [r5], -256
// CHECK: lfetch.count [r2], 1, 64 // encoding: [0x00,0x20,0x28,0x00,0xcb,0x00]
	lfetch.count [r2], 1, 64
// CHECK: lfetch.count [r22], 5, -64 // encoding: [0x00,0xe1,0x6b,0x01,0xcb,0x00]
	lfetch.count.d0 [r22], 5, -64
// CHECK: lfetch.count.nt1 [r23], 9, 960 // encoding: [0x00,0xe2,0x79,0x11,0xcb,0x00]
	lfetch.count.nt1 [r23], 9, 960
// CHECK: lfetch.count.nt2 [r15], 20, -256 // encoding: [0xc0,0x84,0xfb,0x20,0xcb,0x00]
	lfetch.count.d2 [r15], 20, -256
// CHECK: lfetch.count.nta [r125], 24, 512 // encoding: [0xc0,0x05,0xd9,0x37,0xcb,0x00]
	lfetch.count.nta [r125], 24, 512
// CHECK: lfetch.count.d4 [r18], 34, -1024 // encoding: [0x40,0x18,0x2a,0x01,0xcb,0x00]
	lfetch.count.d4 [r18], 34, -1024
// CHECK: lfetch.count.d7 [r96], 64, 0 // encoding: [0xc0,0x1f,0x08,0x36,0xcb,0x00]
	lfetch.count.d7 [r96], 64, 0
// CHECK: (p6) lfetch.count.d5 [r127], 62, 896 // encoding: [0x46,0xdf,0xf9,0x17,0xcb,0x00]
	(p6) lfetch.count.d5 [r127], 62, 896
// CHECK: ld16 r4, ar.csd = [r5] // encoding: [0x00,0x01,0x50,0x08,0x8a,0x00]
	ld16 r4 = [r5]
// CHECK: ld16.acq.nta r4, ar.csd = [r5] // encoding: [0x00,0x01,0x50,0x38,0x8b,0x00]
	ld16.acq.nta r4 = [r5]
// CHECK: st16 [r5] = r4, ar.csd // encoding: [0x00,0x80,0x50,0x08,0x8c,0x00]
	st16 [r5] = r4
// CHECK: st16.rel.d4 [r5] = r4, ar.csd // encoding: [0x00,0x90,0x50,0x08,0x8d,0x00]
	st16.rel.d4 [r5] = r4
// CHECK: cmpxchg1.acq r4 = [r5], r6, ar.ccv // encoding: [0x00,0xc1,0x50,0x08,0x80,0x00]
	cmpxchg1.acq r4 = [r5], r6
// CHECK: cmpxchg8.rel.nta r4 = [r5], r6, ar.ccv // encoding: [0x00,0xc1,0x50,0xf8,0x81,0x00]
	cmpxchg8.rel.nta r4 = [r5], r6
// CHECK: cmp8xchg16.acq r4 = [r5], r6, ar.csd, ar.ccv // encoding: [0x00,0xc1,0x50,0x08,0x88,0x00]
	cmp8xchg16.acq r4 = [r5], r6
// CHECK: cmp8xchg16.rel.nt1 r4 = [r5], r6, ar.csd, ar.ccv // encoding: [0x00,0xc1,0x50,0x18,0x89,0x00]
	cmp8xchg16.rel.nt1 r4 = [r5], r6
// CHECK: mov dahr0 = 0 // encoding: [0x00,0x04,0x00,0x0c,0x00,0x00]
	mov dahr0 = 0
// CHECK: mov dahr3 = 65535 // encoding: [0xc0,0xf7,0xff,0x0d,0x10,0x00]
	mov dahr3 = 0xffff
// CHECK: mov dahr7 = 33825 // encoding: [0x40,0x24,0x84,0x0f,0x10,0x00]
	mov dahr7 = 0x8421
// CHECK: (p7) mov dahr5 = 5 // encoding: [0x47,0x05,0x80,0x0e,0x00,0x00]
	(p7) mov DAHR5 = 5
// CHECK: mov r12 = dahr[r5] // encoding: [0x00,0x03,0x50,0x00,0x21,0x00]
	mov r12 = dahr[r5]
// CHECK: mov r122 = dahr[r55] // encoding: [0x80,0x1e,0x70,0x03,0x21,0x00]
	mov r122 = dahr[r55]

// DIS: ld8.nt2{{[[:space:]]+}}r4 = [r5]
// DIS: ld8.d4{{[[:space:]]+}}r4 = [r5]
// DIS: ld16.d4{{[[:space:]]+}}r4, ar.csd = [r5]
// DIS: st8.d4{{[[:space:]]+}}[r5] = r4
// DIS: lfetch{{[[:space:]]+}}[r5]
// DIS: lfetch.fault.excl.d7{{[[:space:]]+}}[r5], -256
// DIS: lfetch.count{{[[:space:]]+}}[r2], 1, 64
// DIS: lfetch.count.d4{{[[:space:]]+}}[r18], 34, -1024
// DIS: (p6) lfetch.count.d5{{[[:space:]]+}}[r127], 62, 896
// DIS: mov{{[[:space:]]+}}dahr3 = 65535
// DIS: mov{{[[:space:]]+}}r12 = dahr[r5]
