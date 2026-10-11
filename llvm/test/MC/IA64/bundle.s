// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -d - | FileCheck %s

// Instructions are packed into bundles as GNU as packs them, which this file
// was checked against.

	.text

// One instruction takes the first template with a slot for it.
c0:
	add r10 = r2, r3
// CHECK-LABEL: <c0>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c1:
	ld8 r10 = [r2]
// CHECK-LABEL: <c1>:
// CHECK-NEXT: ld8 r10 = [r2] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c2:
	zxt1 r10 = r2
// CHECK-LABEL: <c2>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c3:
	fadd f10 = f6, f7
// CHECK-LABEL: <c3>:
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f10 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c4:
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c4>:
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c5:
	movl r10 = 0x123456789
// CHECK-LABEL: <c5>:
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r10 = 4886718345{{$}}

// The template is the first, in encoding order, that holds the most of the
// next three instructions.
c6:
	add r10 = r2, r3
	add r11 = r2, r3
// CHECK-LABEL: <c6>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c7:
	add r10 = r2, r3
	ld8 r11 = [r2]
// CHECK-LABEL: <c7>:
// CHECK-NEXT: add r10 = r2, r3 // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c8:
	ld8 r10 = [r2]
	ld8 r11 = [r2]
// CHECK-LABEL: <c8>:
// CHECK-NEXT: ld8 r10 = [r2] // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c9:
	add r10 = r2, r3
	fadd f11 = f6, f7
// CHECK-LABEL: <c9>:
// CHECK-NEXT: add r10 = r2, r3 // [MFI]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c10:
	add r10 = r2, r3
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c10>:
// CHECK-NEXT: add r10 = r2, r3 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c11:
	add r10 = r2, r3
	movl r11 = 0x123456789
// CHECK-LABEL: <c11>:
// CHECK-NEXT: add r10 = r2, r3 // [MLX]{{$}}
// CHECK-NEXT: movl r11 = 4886718345{{$}}
c12:
	zxt1 r10 = r2
	add r11 = r2, r3
// CHECK-LABEL: <c12>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
c13:
	zxt1 r10 = r2
	fadd f11 = f6, f7
// CHECK-LABEL: <c13>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c14:
	zxt1 r10 = r2
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c14>:
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c15:
	fadd f10 = f6, f7
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c15>:
// CHECK-NEXT: nop.m 0 // [MFB]{{$}}
// CHECK-NEXT: fma f10 = f6, f1, f7{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c16:
	(p6) br.cond.dptk.few b6
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c16>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c17:
	add r10 = r2, r3
	add r11 = r2, r3
	add r12 = r2, r3
// CHECK-LABEL: <c17>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c18:
	add r10 = r2, r3
	add r11 = r2, r3
	ld8 r12 = [r2]
// CHECK-LABEL: <c18>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r12 = [r2] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c19:
	add r10 = r2, r3
	add r11 = r2, r3
	fadd f12 = f6, f7
// CHECK-LABEL: <c19>:
// CHECK-NEXT: add r10 = r2, r3 // [MMF]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: fma f12 = f6, f1, f7{{$}}
c20:
	add r10 = r2, r3
	add r11 = r2, r3
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c20>:
// CHECK-NEXT: add r10 = r2, r3 // [MIB]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c21:
	add r10 = r2, r3
	ld8 r11 = [r2]
	add r12 = r2, r3
// CHECK-LABEL: <c21>:
// CHECK-NEXT: add r10 = r2, r3 // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c22:
	add r10 = r2, r3
	ld8 r11 = [r2]
	fadd f12 = f6, f7
// CHECK-LABEL: <c22>:
// CHECK-NEXT: add r10 = r2, r3 // [MMF]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: fma f12 = f6, f1, f7{{$}}
c23:
	add r10 = r2, r3
	ld8 r11 = [r2]
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c23>:
// CHECK-NEXT: add r10 = r2, r3 // [MMB]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c24:
	add r10 = r2, r3
	fadd f11 = f6, f7
	add r12 = r2, r3
// CHECK-LABEL: <c24>:
// CHECK-NEXT: add r10 = r2, r3 // [MFI]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c25:
	add r10 = r2, r3
	fadd f11 = f6, f7
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c25>:
// CHECK-NEXT: add r10 = r2, r3 // [MFB]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c26:
	add r10 = r2, r3
	(p6) br.cond.dptk.few b6
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c26>:
// CHECK-NEXT: add r10 = r2, r3 // [MBB]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c27:
	zxt1 r10 = r2
	add r11 = r2, r3
	add r12 = r2, r3
// CHECK-LABEL: <c27>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c28:
	zxt1 r10 = r2
	fadd f11 = f6, f7
	add r12 = r2, r3
// CHECK-LABEL: <c28>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c29:
	(p6) br.cond.dptk.few b6
	(p6) br.cond.dptk.few b6
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c29>:
// CHECK-NEXT: (p6) br.cond.dptk.few b6 // [BBB]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c30:
	movl r10 = 0x123456789
	add r11 = r2, r3
	add r12 = r2, r3
// CHECK-LABEL: <c30>:
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r10 = 4886718345{{$}}
// CHECK-NEXT: add r11 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c31:
	add r10 = r2, r3
	movl r11 = 0x123456789
	add r12 = r2, r3
// CHECK-LABEL: <c31>:
// CHECK-NEXT: add r10 = r2, r3 // [MLX]{{$}}
// CHECK-NEXT: movl r11 = 4886718345{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c32:
	add r10 = r2, r3
	add r11 = r2, r3
	movl r12 = 0x123456789
// CHECK-LABEL: <c32>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r12 = 4886718345{{$}}
c33:
	ld8 r10 = [r2]
	ld8 r11 = [r2]
	ld8 r12 = [r2]
	ld8 r13 = [r2]
// CHECK-LABEL: <c33>:
// CHECK-NEXT: ld8 r10 = [r2] // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r12 = [r2] // [MMI]{{$}}
// CHECK-NEXT: ld8 r13 = [r2]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c34:
	add r10 = r2, r3
	add r11 = r2, r3
	add r12 = r2, r3
	add r13 = r2, r3
	add r14 = r2, r3
	add r15 = r2, r3
	add r16 = r2, r3
// CHECK-LABEL: <c34>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
// CHECK-NEXT: add r13 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r14 = r2, r3{{$}}
// CHECK-NEXT: add r15 = r2, r3{{$}}
// CHECK-NEXT: add r16 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// A stop after slot 0 makes the template M;MI.
c35:
	add r10 = r2, r3 ;;
	add r11 = r2, r3
// CHECK-LABEL: <c35>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c36:
	add r10 = r2, r3 ;;
	add r11 = r2, r3
	add r12 = r2, r3
// CHECK-LABEL: <c36>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c37:
	ld8 r10 = [r2] ;;
	zxt1 r11 = r2
// CHECK-LABEL: <c37>:
// CHECK-NEXT: ld8 r10 = [r2] ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: zxt1 r11 = r2{{$}}
c38:
	add r10 = r2, r3 ;;
	fadd f11 = f6, f7
// CHECK-LABEL: <c38>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f11 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c39:
	add r10 = r2, r3 ;;
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c39>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c40:
	add r10 = r2, r3 ;;
	movl r11 = 0x123456789
// CHECK-LABEL: <c40>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r11 = 4886718345{{$}}
c41:
	add r10 = r2, r3 ;;
	ld8 r11 = [r2] ;;
	add r12 = r2, r3
// CHECK-LABEL: <c41>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// A stop after an I unit in slot 1 makes it MI;I.
c42:
	add r10 = r2, r3
	add r11 = r2, r3 ;;
	add r12 = r2, r3
// CHECK-LABEL: <c42>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3{{$}}
c43:
	zxt1 r10 = r2 ;;
	add r11 = r2, r3
// CHECK-LABEL: <c43>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2 ;;{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
c44:
	zxt1 r10 = r2 ;;
	zxt1 r11 = r2
// CHECK-LABEL: <c44>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2 ;;{{$}}
// CHECK-NEXT: zxt1 r11 = r2{{$}}
c45:
	zxt1 r10 = r2 ;;
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c45>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2 ;;{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c46:
	zxt1 r10 = r2 ;;
	ld8 r11 = [r2]
// CHECK-LABEL: <c46>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2 ;;{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r11 = [r2] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c47:
	add r10 = r2, r3
	add r11 = r2, r3 ;;
	fadd f12 = f6, f7
// CHECK-LABEL: <c47>:
// CHECK-NEXT: add r10 = r2, r3 // [MMF]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.f 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f12 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// An alloc, a flushrs or a loadrs is the first instruction of its group, and
// not the one after a nop that is. The stop before one that begins the next
// bundle is at the end of this one.
c47a:
	zxt1 r10 = r2 ;;
	alloc r11 = ar.pfs, 0, 0, 2, 0
// CHECK-LABEL: <c47a>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: alloc r11 = ar.pfs, 0, 0, 2, 0 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c47b:
	ld8 r10 = [r2]
	zxt1 r11 = r2 ;;
	flushrs
// CHECK-LABEL: <c47b>:
// CHECK-NEXT: ld8 r10 = [r2] // [MII]{{$}}
// CHECK-NEXT: zxt1 r11 = r2{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: flushrs // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c47c:
	zxt1 r10 = r2 ;;
	loadrs
// CHECK-LABEL: <c47c>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: loadrs // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// A stop anywhere else ends the bundle.
c48:
	add r10 = r2, r3
	add r11 = r2, r3 ;;
// CHECK-LABEL: <c48>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
c49:
	add r10 = r2, r3
	add r11 = r2, r3
	add r12 = r2, r3 ;;
// CHECK-LABEL: <c49>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: add r12 = r2, r3 ;;{{$}}
c50:
	fadd f10 = f6, f7 ;;
	add r11 = r2, r3
// CHECK-LABEL: <c50>:
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f10 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r11 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c51:
	fadd f10 = f6, f7 ;;
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c51>:
// CHECK-NEXT: nop.m 0 // [MFB]{{$}}
// CHECK-NEXT: fma f10 = f6, f1, f7{{$}}
// CHECK-NEXT: nop.b 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c52:
	(p6) br.cond.dptk.few b6 ;;
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c52>:
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
// CHECK-NEXT: nop.b 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c53:
	(p6) br.cond.dptk.few b6 ;;
	(p6) br.cond.dptk.few b6
	(p6) br.cond.dptk.few b6
// CHECK-LABEL: <c53>:
// CHECK-NEXT: (p6) br.cond.dptk.few b6 // [BBB]{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: nop.b 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MBB]{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
// CHECK-NEXT: (p6) br.cond.dptk.few b6{{$}}
c54:
	add r10 = r2, r3
	ld8 r11 = [r2] ;;
	add r12 = r2, r3
// CHECK-LABEL: <c54>:
// CHECK-NEXT: add r10 = r2, r3 // [MMI]{{$}}
// CHECK-NEXT: ld8 r11 = [r2]{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c55:
	add r10 = r2, r3 ;;
	add r11 = r2, r3 ;;
	add r12 = r2, r3
// CHECK-LABEL: <c55>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c56:
	add r10 = r2, r3
	movl r11 = 0x123456789 ;;
	add r12 = r2, r3
// CHECK-LABEL: <c56>:
// CHECK-NEXT: add r10 = r2, r3 // [MLX]{{$}}
// CHECK-NEXT: movl r11 = 4886718345 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
c57:
	movl r10 = 0x123456789 ;;
	movl r11 = 0x123456789
// CHECK-LABEL: <c57>:
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r10 = 4886718345 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r11 = 4886718345{{$}}

// A stop after the last instruction before a label is at the end of the
// bundle.
c58:
	add r10 = r2, r3 ;;
// CHECK-LABEL: <c58>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
c59:
	zxt1 r10 = r2 ;;
// CHECK-LABEL: <c59>:
// CHECK-NEXT: nop.m 0 // [MII]{{$}}
// CHECK-NEXT: zxt1 r10 = r2{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
c60:
	add r10 = r2, r3 ;;
	add r11 = r2, r3 ;;
// CHECK-LABEL: <c60>:
// CHECK-NEXT: add r10 = r2, r3 ;; // [MMI]{{$}}
// CHECK-NEXT: add r11 = r2, r3{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
c61:
	add r10 = r2, r3
	add r11 = r2, r3 ;;
	add r12 = r2, r3 ;;
// CHECK-LABEL: <c61>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: add r11 = r2, r3 ;;{{$}}
// CHECK-NEXT: add r12 = r2, r3 ;;{{$}}

// Padding to a code alignment is nop bundles. An alignment and the end of a
// section end the instruction group.
align:
	add r10 = r2, r3
	.align 32
	add r11 = r2, r3
	.align 32
	add r12 = r2, r3
// CHECK-LABEL: <align>:
// CHECK-NEXT: add r10 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r11 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: add r12 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r14 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
	.section .text.other,"ax",@progbits
	add r13 = r2, r3
	.text
	add r14 = r2, r3

// CHECK-LABEL: <.text.other>:
// CHECK-NEXT: add r13 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}

// A label is on a bundle, so the one before it ends there, but the
// instruction group does not: it ends when something else ends it, even with
// nothing more to put in a bundle.
	.section .text.label,"ax",@progbits
	add r15 = r2, r3
.Lmid:
	add r16 = r2, r3
.Lend:
	.section .text.label2,"ax",@progbits
	add r17 = r2, r3
.Lstop:
	;;
	add r18 = r2, r3

// CHECK-LABEL: <.text.label>:
// CHECK-NEXT: add r15 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: add r16 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-LABEL: <.text.label2>:
// CHECK-NEXT: add r17 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r18 = r2, r3 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
