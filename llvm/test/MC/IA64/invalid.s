// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

// CHECK: :[[#@LINE+1]]:11: error: immediate must be an integer in [-128, 127]
	sub r1 = 128, r2
// CHECK: :[[#@LINE+1]]:12: error: immediate must be an integer in [-8192, 8191]
	adds r1 = 8192, r2
// CHECK: :[[#@LINE+1]]:12: error: immediate must be an integer in [-2097152, 2097151]
	addl r1 = -2097153, r2
// addl has a two-bit base register field.
// CHECK: :[[#@LINE+1]]:15: error: invalid operand for instruction
	addl r1 = 1, r4
// CHECK: :[[#@LINE+1]]:18: error: shift count must be an integer in [1, 4]
	shladd r1 = r2, 5, r3
// CHECK: :[[#@LINE+1]]:20: error: shift count must be an integer in [1, 3]
	pshladd2 r1 = r2, 4, r3
// CHECK: :[[#@LINE+1]]:6: error: invalid operand for instruction
	add f1 = r2, r3
// CHECK: :[[#@LINE+1]]:3: error: expected a predicate register
	(r6) add r1 = r2, r3
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	frob r1 = r2, r3
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	add r1 = r2

// I-unit operands.
// CHECK: :[[#@LINE+1]]:26: error: invalid operand for instruction
	pmpyshr2.u r4 = r5, r6, 8
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	mux2 r4 = r5, 256
// CHECK: :[[#@LINE+1]]:16: error: unknown operand '@foo'
	mux1 r4 = r5, @foo
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	pshr4.u r4 = r6, 32
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	extr r4 = r6, 5, 0
// CHECK: :[[#@LINE+1]]:13: error: immediate must be an integer in [-128, 127]
	dep.z r4 = 128, 40, 9
// CHECK: :[[#@LINE+1]]:11: error: invalid operand for instruction
	dep r4 = 1, r6, 20, 33
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	dep r4 = r5, r6, 17, 17
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	tf.z p6, p7 = 31
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.i 0x200000

// An IP-relative target is a whole number of bundles away.
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, 8
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, 0x1000000
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.i r4, -0x1000010
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-4096, 4080]
	mov b6 = r4, 4096
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-4096, 4080]
	mov b6 = r4, 24

// M-unit operands. The hint has a number of 19 bits.
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	hint.m 0x80000

// A load takes no .nt2 hint and a store only .nta.
// CHECK: :[[#@LINE+1]]:5: error: invalid operand for instruction
	ld8.nt2 r4 = [r5]
// CHECK: :[[#@LINE+1]]:5: error: invalid operand for instruction
	st8.nt1 [r4] = r5
// CHECK: :[[#@LINE+1]]:5: error: invalid operand for instruction
	add.nta r4 = r5, r6
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	flushrs.nta
// CHECK: :[[#@LINE+1]]:17: error: invalid operand for instruction
	ld8 r4 = [r5], 256
// CHECK: :[[#@LINE+1]]:17: error: invalid operand for instruction
	ld8 r4 = [r5], -257
// CHECK: :[[#@LINE+1]]:17: error: invalid operand for instruction
	st8 [r4] = r5, r6
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	ldfps f4, f5 = [r6], 16
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	ldfpd f4, f5 = [r6], 8
// CHECK: :[[#@LINE+1]]:27: error: invalid operand for instruction
	fetchadd8.acq r4 = [r5], 2
// CHECK: :[[#@LINE+1]]:30: error: invalid operand for instruction
	cmpxchg8.acq r4 = [r5], r6, ar.csd
// CHECK: :[[#@LINE+1]]:6: error: invalid operand for instruction
	mov cpuid[r5] = r4
// CHECK: :[[#@LINE+1]]:6: error: invalid operand for instruction
	sum 0x1000000
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	probe.r r4 = r5, 4
// CHECK: :[[#@LINE+1]]:17: error: immediate must be an integer in [-128, 127]
	mov.m ar.ccv = 128
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.m 0x200000

// alloc describes a frame of at most 96 registers, and rotates them in
// groups of eight.
// CHECK: :[[#@LINE+1]]:30: error: number of rotating registers must be a multiple of 8
	alloc r4 = ar.pfs, 2, 3, 4, 7
// CHECK: :[[#@LINE+1]]:30: error: more rotating registers than the frame has
	alloc r4 = ar.pfs, 2, 3, 4, 16
// CHECK: :[[#@LINE+1]]:21: error: register stack frame is larger than 96 registers
	alloc r4 = ar.pfs, 90, 3, 4, 0
// CHECK: :[[#@LINE+1]]:13: error: invalid operand for instruction
	alloc r4 = ar.lc, 2, 3, 4, 0
// CHECK: :[[#@LINE+1]]:3: error: alloc takes no predicate
	(p6) alloc r4 = ar.pfs, 2, 3, 4, 0

// A move written with its unit names an application register that the
// unit has.
// CHECK: :[[#@LINE+1]]:8: error: application register is not one of the M unit
	mov.m ar.lc = r4
// CHECK: :[[#@LINE+1]]:8: error: application register is not one of the M unit
	mov.m ar.pfs = 5
// CHECK: :[[#@LINE+1]]:13: error: application register is not one of the M unit
	mov.m r4 = ar.ec
// CHECK: :[[#@LINE+1]]:8: error: application register is not one of the I unit
	mov.i ar.k0 = r4
// CHECK: :[[#@LINE+1]]:8: error: application register is not one of the I unit
	mov.i ar.fpsr = 5
// CHECK: :[[#@LINE+1]]:13: error: application register is not one of the I unit
	mov.i r4 = ar.unat

// A register that is read from by its index needs the index.
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	mov r4 = cpuid
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	mov r4 = pmd[r5

// As in the I-unit, a check's target is a whole number of bundles away.
// CHECK: :[[#@LINE+1]]:14: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.s.m r4, 8
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.a.nc r4, 0x1000000
// CHECK: :[[#@LINE+1]]:16: error: target must be a multiple of 16 in [-16777216, 16777200]
	chk.a.clr f4, 17

// F-unit operands. There are four status fields, and only an instruction
// that can raise a floating-point exception names one.
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	fma.s4 f4 = f5, f6, f7
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	fmerge.s.s1 f4 = f5, f6
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	fcvt.xf.s1 f4 = f5
// CHECK: :[[#@LINE+1]]:5: error: invalid operand for instruction
	ld8.s1 r4 = [r5]
// CHECK: :[[#@LINE+1]]:24: error: invalid operand for instruction
	fclass.m p6, p7 = f4, 0x200
// CHECK: :[[#@LINE+1]]:8: error: invalid operand for instruction
	fsetc 128, 0
// CHECK: :[[#@LINE+1]]:19: error: invalid operand for instruction
	fcmp.eq p6, p7 = r4, f5
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	fma f4 = f5, f6
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.f 0x200000
// CHECK: :[[#@LINE+1]]:8: error: target must be a multiple of 16 in [-16777216, 16777200]
	fchkf 0x1000000
// CHECK: :[[#@LINE+1]]:11: error: target must be a multiple of 16 in [-16777216, 16777200]
	fchkf.s1 4

// B-unit operands. A branch names its whether hint, then optionally its
// prefetch hint and ".clr"; brp spells its hints as part of the mnemonic.
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	br.cond 16
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	br.cond.few 16
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	br.cond.sptk.clr.few 16
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	br.cond.few.sptk 16
// CHECK: :[[#@LINE+1]]:15: error: invalid operand for instruction
	br.call.sptk 16
// CHECK: :[[#@LINE+1]]:15: error: invalid operand for instruction
	br.call.sptk r4 = 16
// CHECK: :[[#@LINE+1]]:14: error: invalid operand for instruction
	br.ret.sptk r4
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	br.ret.sptk.many
// CHECK: :[[#@LINE+1]]:11: error: invalid operand for instruction
	brp.loop b6, 16
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	brp.sptk.few 16, 32
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	ld8.clr r4 = [r5]
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	epc.few
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.b 0x200000
// The counted and modulo-scheduled loop branches, brp and the instructions
// that change privileged state take no qualifying predicate.
// CHECK: :[[#@LINE+1]]:3: error: invalid operand for instruction
	(p6) br.cloop.sptk 16
// CHECK: :[[#@LINE+1]]:3: error: invalid operand for instruction
	(p6) br.ctop.sptk 16
// CHECK: :[[#@LINE+1]]:3: error: invalid operand for instruction
	(p6) brp.sptk 16, 32
// CHECK: :[[#@LINE+1]]:3: error: invalid operand for instruction
	(p6) cover
// CHECK: :[[#@LINE+1]]:3: error: invalid operand for instruction
	(p6) rfi
// Branch targets and brp tags are checked the same way.
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-16777216, 16777200]
	br.cond.sptk 8
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-16777216, 16777200]
	br.cond.sptk 0x1000000
// CHECK: :[[#@LINE+1]]:20: error: target must be a multiple of 16 in [-16777216, 16777200]
	br.call.sptk b0 = -0x1000010
// CHECK: :[[#@LINE+1]]:20: error: target must be a multiple of 16 in [-16777216, 16777200]
	(p6) br.cond.sptk 8
// CHECK: :[[#@LINE+1]]:16: error: target must be a multiple of 16 in [-16777216, 16777200]
	br.cloop.sptk 20
// CHECK: :[[#@LINE+1]]:15: error: target must be a multiple of 16 in [-4096, 4080]
	brp.sptk 16, 4096
// CHECK: :[[#@LINE+1]]:11: error: target must be a multiple of 16 in [-16777216, 16777200]
	brp.sptk 8, 32
// CHECK: :[[#@LINE+1]]:19: error: target must be a multiple of 16 in [-4096, 4080]
	brp.ret.sptk b0, -4112

// X-unit operands. Only cond and call have long forms, and a long branch
// reaches any bundle.
// CHECK: :[[#@LINE+1]]:11: error: invalid operand for instruction
	brl.cond 16
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	brl.wexit.sptk 16
// CHECK: :[[#@LINE+1]]:16: error: target must be a multiple of 16
	brl.cond.sptk 8
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	brl.cond.sptk b6
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	brl.call.sptk 16
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	brl.call.sptk r4 = 16
// CHECK: :[[#@LINE+1]]:12: error: invalid operand for instruction
	movl r4 = r5
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	movl r4
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.x 0x4000000000000000
// CHECK: :[[#@LINE+1]]:10: error: invalid operand for instruction
	break.x -1
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	nop.x.sptk 0

// The pseudo-ops.
// CHECK: :[[#@LINE+1]]:17: error: invalid operand for instruction
	add r4 = 8192, r5
// CHECK: :[[#@LINE+1]]:11: error: immediate must be an integer in [-2097152, 2097151]
	add r4 = 2097152, r0
// CHECK: :[[#@LINE+1]]:11: error: immediate must be an integer in [-2097152, 2097151]
	mov r4 = 2097152
// CHECK: :[[#@LINE+1]]:2: error: too few operands for instruction
	mov.imp b6 = r4
// CHECK: :[[#@LINE+1]]:15: error: shift count must be an integer in [0, 63]
	shl r4 = r5, 64
// CHECK: :[[#@LINE+1]]:17: error: shift count must be an integer in [0, 63]
	shr.u r4 = r5, -1
// CHECK: :[[#@LINE+1]]:18: error: immediate must be an integer in [-127, 128]
	cmp.le p6, p7 = 129, r5
// CHECK: :[[#@LINE+1]]:22: error: immediate must be an integer in [-127, 128]
	cmp.gt.unc p6, p7 = -128, r5
// CHECK: :[[#@LINE+1]]:19: error: immediate must be a nonzero integer in [-127, 128]
	cmp.leu p6, p7 = 0, r5
// CHECK: :[[#@LINE+1]]:20: error: immediate must be a nonzero integer in [-127, 128]
	cmp4.gtu p6, p7 = 129, r5
// CHECK: :[[#@LINE+1]]:18: error: immediate must be an integer in [-128, 127]
	cmp.ne p6, p7 = 128, r5
// CHECK: :[[#@LINE+1]]:25: error: invalid operand for instruction
	cmp.lt.or p6, p7 = r4, r5
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	cmp.lt.orcm p6, p7 = 10, r5
// CHECK: :[[#@LINE+1]]:28: error: invalid operand for instruction
	cmp.ge.andcm p6, p7 = r4, r5
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	br b6, 16
// CHECK: :[[#@LINE+1]]:9: error: invalid operand for instruction
	br.few r4
// CHECK: :[[#@LINE+1]]:6: error: invalid operand for instruction
	brl b6
// CHECK: :[[#@LINE+1]]:23: error: invalid operand for instruction
	fadd.s3 f4 = f5, f6, f7
// CHECK: :[[#@LINE+1]]:16: error: invalid operand for instruction
	fabs f4 = f5, f6
// CHECK: :[[#@LINE+1]]:25: error: invalid operand for instruction
	fclass.nm p6, p7 = f4, 0x200
// CHECK: :[[#@LINE+1]]:22: error: invalid operand for instruction
	xmpy.l f4 = f5, f6, f7
// CHECK: :[[#@LINE+1]]:28: error: invalid operand for instruction
	tbit.nz.orcm p6, p7 = r4, 64

// Relocation specifiers.
// CHECK: :[[#@LINE+1]]:12: error: unknown operand '@bogus'
	addl r4 = @bogus(x), r1
// CHECK: :[[#@LINE+1]]:19: error: this relocation specifier cannot be inside '@gprel'
	addl r4 = @gprel(@ltoff(x)), r1
// CHECK: :[[#@LINE+1]]:19: error: expected '(' after '@gprel'
	addl r4 = @gprel x, r1
// CHECK: :[[#@LINE+1]]:20: error: expected ')'
	addl r4 = @gprel(x, r1
// CHECK: :[[#@LINE+1]]:12: error: expected a name after '@'
	addl r4 = @(x), r1

// Data directives.
// CHECK: :[[#@LINE+1]]:8: error: a floating-point number is not a value here
	data8 1.5
// CHECK: :[[#@LINE+1]]:8: error: a floating-point number is not a value here
	.long 1e3
// CHECK: :[[#@LINE+1]]:9: error: a floating-point number is not a value here
	data16 1.5
// CHECK: :[[#@LINE+1]]:10: error: unexpected token
	data8 1 2
// CHECK: :[[#@LINE+1]]:10: error: expected string
	stringz 3
// CHECK: :[[#@LINE+1]]:2: error: unrecognized instruction mnemonic
	data1.ua 3
// CHECK: :[[#@LINE+1]]:2: error: unknown directive
	.long.ua 3

// The '#' of "name#" has to follow the name directly.
// CHECK: :[[#@LINE+1]]:14: error: unknown token in expression
	add r4 = r5 #, r6
// CHECK: :[[#@LINE+1]]:12: error: unexpected token
	.global f #

// "." is the bundle of the instruction, which only a branch tells its
// target from.
// CHECK: :[[#@LINE+1]]:12: error: '.' is only for the target of a branch here
	movl r1 = .
// CHECK: :[[#@LINE+1]]:12: error: '.' is only for the target of a branch here
	adds r1 = .+4, r2
// CHECK: :[[#@LINE+1]]:16: error: expected absolute expression
	br.cond.sptk .+undefined
