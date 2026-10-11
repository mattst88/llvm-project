// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// What the instructions of a prologue do, as the unwind information has it.
// Each procedure here has two words of it: the header, and descriptors
// padded with zeroes. The first is always that of a prologue, with its
// length, and one of a body ends many. GNU as writes the same object for
// this file.

// CHECK: Hex dump of section '.IA_64.unwind_info':

	.text

// A frame of a fixed size: the time, in slots from where the prologue begins,
// and the size in units of 16 bytes.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e00180 02210000
	.proc f1
f1:
	.prologue
	nop 0
	.fframe 4096
	nop 0
	.body
	nop 0
	.endp f1

// A frame of another size, with the previous sp in a register or in memory.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e101b0 24210000
	.proc f2
f2:
	.prologue
	nop 0
	.vframe r36
	nop 0
	.body
	nop 0
	.endp f2
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e100e3 0c220000
	.proc f3
f3:
	.prologue
	.vframesp 48
	nop 0
	.body
	nop 0
	.endp f3

// Where the spill area is, in units of four bytes down from 16 bytes above
// the previous sp.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e210e2 00210000
	.proc f4
f4:
	.prologue
	.spill 48
	nop 0
	.spill -16
	nop 0
	.body
	nop 0
	.endp f4

// A register saved in a general register: when, and which.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e401b0 a3210000
	.proc f5
f5:
	.prologue
	nop 0
	.save rp, r35
	nop 0
	.body
	nop 0
	.endp f5
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e601b1 23210000
	.proc f6
f6:
	.prologue
	nop 0
	.save ar.pfs, r35
	nop 0
	.body
	nop 0
	.endp f6
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02e801b1 a3210000
	.proc f7
f7:
	.prologue
	nop 0
	.save pr, r35
	nop 0
	.body
	nop 0
	.endp f7
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02ec01b2 23210000
	.proc f8
f8:
	.prologue
	nop 0
	.save ar.unat, r35
	nop 0
	.body
	nop 0
	.endp f8
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02ea01b2 a3210000
	.proc f9
f9:
	.prologue
	nop 0
	.save ar.lc, r35
	nop 0
	.body
	nop 0
	.endp f9
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02ee01b5 23210000
	.proc f10
f10:
	.prologue
	nop 0
	.save ar.fpsr, r35
	nop 0
	.body
	nop 0
	.endp f10
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02f00d01 b3a32100
	.proc f11
f11:
	.prologue
	nop 0
	.save ar.rnat, r35
	nop 0
	.body
	nop 0
	.endp f11
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02f00701 b4232100
	.proc f12
f12:
	.prologue
	nop 0
	.save ar.bsp, r35
	nop 0
	.body
	nop 0
	.endp f12
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02f00a01 b4a32100
	.proc f13
f13:
	.prologue
	nop 0
	.save ar.bspstore, r35
	nop 0
	.body
	nop 0
	.endp f13
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02f01001 b5a32100
	.proc f14
f14:
	.prologue
	nop 0
	.save @priunat, r35
	nop 0
	.body
	nop 0
	.endp f14

// The registers by other names.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e400b0 82220000
	.proc f15
f15:
	.prologue
	.save b0, r2
	nop 0
	.body
	nop 0
	.endp f15
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e600b1 03220000
	.proc f16
f16:
	.prologue
	.save ar64, r3
	nop 0
	.body
	nop 0
	.endp f16
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ec00b2 04220000
	.proc f17
f17:
	.prologue
	.save ar36, r4
	nop 0
	.body
	nop 0
	.endp f17
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ea00b2 85220000
	.proc f18
f18:
	.prologue
	.save ar65, r5
	nop 0
	.body
	nop 0
	.endp f18
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ee00b5 06220000
	.proc f19
f19:
	.prologue
	.save ar40, r6
	nop 0
	.body
	nop 0
	.endp f19
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00d00 b3872200
	.proc f20
f20:
	.prologue
	.save ar19, r7
	nop 0
	.body
	nop 0
	.endp f20
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00700 b4082200
	.proc f21
f21:
	.prologue
	.save ar17, r8
	nop 0
	.body
	nop 0
	.endp f21
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00a00 b4892200
	.proc f22
f22:
	.prologue
	.save ar18, r9
	nop 0
	.body
	nop 0
	.endp f22

// A register saved in memory, in units of four bytes up from sp.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e402f0 01100000
	.proc f23
f23:
	.prologue
	nop 0
	nop 0
	.savesp rp, 64
	nop 0
	.endp f23
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e602f0 02100000
	.proc f24
f24:
	.prologue
	nop 0
	nop 0
	.savesp ar.pfs, 64
	nop 0
	.endp f24
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03e802f0 03100000
	.proc f25
f25:
	.prologue
	nop 0
	nop 0
	.savesp pr, 64
	nop 0
	.endp f25
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03ec02f0 05100000
	.proc f26
f26:
	.prologue
	nop 0
	nop 0
	.savesp ar.unat, 64
	nop 0
	.endp f26
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03ea02f0 04100000
	.proc f27
f27:
	.prologue
	nop 0
	nop 0
	.savesp ar.lc, 64
	nop 0
	.endp f27
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03ee02f0 06100000
	.proc f28
f28:
	.prologue
	nop 0
	nop 0
	.savesp ar.fpsr, 64
	nop 0
	.endp f28
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f00d02 f00f1000
	.proc f29
f29:
	.prologue
	nop 0
	nop 0
	.savesp ar.rnat, 64
	nop 0
	.endp f29
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f00702 f0091000
	.proc f30
f30:
	.prologue
	nop 0
	nop 0
	.savesp ar.bsp, 64
	nop 0
	.endp f30
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f00a02 f00c1000
	.proc f31
f31:
	.prologue
	nop 0
	nop 0
	.savesp ar.bspstore, 64
	nop 0
	.endp f31
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 03f01302 f0121000
	.proc f32
f32:
	.prologue
	nop 0
	nop 0
	.savesp @priunat, 64
	nop 0
	.endp f32

// Or down from 16 bytes above the previous sp.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e400e5 0c220000
	.proc f33
f33:
	.prologue
	.savepsp rp, 32
	nop 0
	.body
	nop 0
	.endp f33
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e600e7 0c220000
	.proc f34
f34:
	.prologue
	.savepsp ar.pfs, 32
	nop 0
	.body
	nop 0
	.endp f34
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01e800e9 0c220000
	.proc f35
f35:
	.prologue
	.savepsp pr, 32
	nop 0
	.body
	nop 0
	.endp f35
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ec00ed 0c220000
	.proc f36
f36:
	.prologue
	.savepsp ar.unat, 32
	nop 0
	.body
	nop 0
	.endp f36
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ea00eb 0c220000
	.proc f37
f37:
	.prologue
	.savepsp ar.lc, 32
	nop 0
	.body
	nop 0
	.endp f37
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ee00ef 0c220000
	.proc f38
f38:
	.prologue
	.savepsp ar.fpsr, 32
	nop 0
	.body
	nop 0
	.endp f38
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00d00 f00e0c22
	.proc f39
f39:
	.prologue
	.savepsp ar.rnat, 32
	nop 0
	.body
	nop 0
	.endp f39
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00700 f0080c22
	.proc f40
f40:
	.prologue
	.savepsp ar.bsp, 32
	nop 0
	.body
	nop 0
	.endp f40
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f00a00 f00b0c22
	.proc f41
f41:
	.prologue
	.savepsp ar.bspstore, 32
	nop 0
	.body
	nop 0
	.endp f41
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01f01300 f0110c22
	.proc f42
f42:
	.prologue
	.savepsp @priunat, 32
	nop 0
	.body
	nop 0
	.endp f42

// The return address in another branch register.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 02b307b3 00210000
	.proc f43
f43:
	.prologue
	.altrp b7
	nop 0
	.altrp rp
	nop 0
	.body
	nop 0
	.endp f43

// Other conventions than these, by name or by number.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ff0000 ff010122
	.proc f44
f44:
	.prologue
	.unwabi @svr4, 0
	.unwabi @hpux, 1
	nop 0
	.body
	nop 0
	.endp f44
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 01ff02c8 ff036922
	.proc f45
f45:
	.prologue
	.unwabi @nt, 200
	.unwabi 3, 'i'
	nop 0
	.body
	nop 0
	.endp f45

// With a mask, the prologue has said which register holds rp, ar.pfs, the
// previous sp and pr, and only the time is left to tell.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 47a002e4 00e60121
	.proc f46
f46:
	.prologue 15, 32
	.save rp, r32
	nop 0
	.save ar.pfs, r33
	nop 0
	.body
	nop 0
	.endp f46
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 47a002e1 00e80121
	.proc f47
f47:
	.prologue 15, 32
	.vframe r34
	nop 0
	.save pr, r35
	nop 0
	.body
	nop 0
	.endp f47

// What the mask leaves out is told in full.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 41a801e4 00b08222
	.proc f48
f48:
	.prologue 3, 40
	.save rp, r2
	nop 0
	.body
	nop 0
	.endp f48

// Times are from the beginning of the region, and may be large.
// CHECK-NEXT: 0x{{[0-9a-f]+}} 01000000 00000100 608401ea 8201b282
	.proc f49
f49:
	.prologue
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	nop 0
	.save ar.lc, r2
	nop 0
	.endp f49
