// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-objdump -d -z - | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM

// The template directives. The bundles are those of GNU as, which this file
// was checked against.

// ASM:      .mmi
// ASM-NEXT: add r4 = r5, r6

	.text

// A template directive names the template of the next bundle. Instructions
// take its slots in order, each the first that has its unit.
t0:
	.mmi
	add r4 = r5, r6
// CHECK-LABEL: <t0>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t1:
	.mmi
	add r4 = r5, r6
	add r4 = r5, r6
	add r4 = r5, r6
// CHECK-LABEL: <t1>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
t2:
	.mfi
	add r4 = r5, r6
	add r4 = r5, r6
// CHECK-LABEL: <t2>:
// CHECK-NEXT: add r4 = r5, r6 // [MFI]{{$}}
// CHECK-NEXT: nop.f 0{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
t3:
	.mib
	shr r4 = r5, 3
// CHECK-LABEL: <t3>:
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: shr r4 = r5, 3{{$}}
// CHECK-NEXT: nop.b 0{{$}}
t4:
	.mmb
	br.ret.sptk.many b0
// CHECK-LABEL: <t4>:
// CHECK-NEXT: nop.m 0 // [MMB]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
t5:
	.mlx
	movl r4 = 5
// CHECK-LABEL: <t5>:
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r4 = 5{{$}}
t6:
	.bbb
	nop 0
	nop 0
	nop 0
// CHECK-LABEL: <t6>:
// CHECK-NEXT: nop.b 0 // [BBB]{{$}}
// CHECK-NEXT: nop.b 0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
t7:
	.MFB
	nop 0
	nop 0
	nop 0
// CHECK-LABEL: <t7>:
// CHECK-NEXT: nop.m 0 // [MFB]{{$}}
// CHECK-NEXT: nop.f 0{{$}}
// CHECK-NEXT: nop.b 0{{$}}
t8:
	.mmf
	nop 0
	nop 0
	nop 0
// CHECK-LABEL: <t8>:
// CHECK-NEXT: nop.m 0 // [MMF]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.f 0{{$}}

// What the template has no slot for goes to the next bundle, which is packed
// as usual.
t9:
	.mii
	ld8 r4 = [r5]
	ld8 r4 = [r5]
// CHECK-LABEL: <t9>:
// CHECK-NEXT: ld8 r4 = [r5] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r4 = [r5] // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t10:
	.mmi
	add r4 = r5, r6
	add r4 = r5, r6
	add r4 = r5, r6
	add r4 = r5, r6
	shr r4 = r5, 3
// CHECK-LABEL: <t10>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: shr r4 = r5, 3{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t11:
	.mib
	br.ret.sptk.many b0
	add r4 = r5, r6
// CHECK-LABEL: <t11>:
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// The last of several directives counts, and one stays in force until there
// is an instruction.
t12:
	.mmi
	.mfi
	add r4 = r5, r6
// CHECK-LABEL: <t12>:
// CHECK-NEXT: add r4 = r5, r6 // [MFI]{{$}}
// CHECK-NEXT: nop.f 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
t13:
	.mmi
	.data
	.text
1:
	ld8 r4 = [r5]
	ld8 r4 = [r5]
// CHECK-LABEL: <t13>:
// CHECK-NEXT: ld8 r4 = [r5] // [MMI]{{$}}
// CHECK-NEXT: ld8 r4 = [r5]{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// Only the first bundle has the template. Instructions before the directive
// are packed as though it were not there, but stop short of it.
t14:
	add r4 = r5, r6
	.mmi
	add r4 = r5, r6
	add r4 = r5, r6
// CHECK-LABEL: <t14>:
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t15:
	add r4 = r5, r6
	.mmi
	ld8 r4 = [r5]
// CHECK-LABEL: <t15>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r4 = [r5] // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t16:
	add r4 = r5, r6
	add r4 = r5, r6
	.mmi
	ld8 r4 = [r5]
// CHECK-LABEL: <t16>:
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: ld8 r4 = [r5] // [MMI]{{$}}
// CHECK-NEXT: nop.m 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}

// MMI has a form with a stop after slot 0 and MII one with a stop after slot
// 1. A stop anywhere else moves to the next place the template has one, and
// nops fill the slots up to it.
t17:
	.mmi
	add r4 = r5, r6 ;;
	add r4 = r5, r6
	add r4 = r5, r6
// CHECK-LABEL: <t17>:
// CHECK-NEXT: add r4 = r5, r6 ;; // [MMI]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
t18:
	.mii
	add r4 = r5, r6
	add r4 = r5, r6 ;;
	add r4 = r5, r6
// CHECK-LABEL: <t18>:
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: add r4 = r5, r6 ;;{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
t19:
	.mii
	add r4 = r5, r6 ;;
	add r4 = r5, r6
	add r4 = r5, r6
// CHECK-LABEL: <t19>:
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t20:
	.mmi
	add r4 = r5, r6
	add r4 = r5, r6 ;;
	add r4 = r5, r6
// CHECK-LABEL: <t20>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: add r4 = r5, r6{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: add r4 = r5, r6 // [MII]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: nop.i 0{{$}}
t21:
	.mfi
	add r4 = r5, r6 ;;
	fadd f6 = f7, f8
	shr r4 = r5, 3
// CHECK-LABEL: <t21>:
// CHECK-NEXT: add r4 = r5, r6 // [MFI]{{$}}
// CHECK-NEXT: nop.f 0{{$}}
// CHECK-NEXT: nop.i 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MFI]{{$}}
// CHECK-NEXT: fma f6 = f7, f1, f8{{$}}
// CHECK-NEXT: shr r4 = r5, 3{{$}}
t22:
	.mib
	add r4 = r5, r6
	shr r4 = r5, 3 ;;
	br.ret.sptk.many b0
// CHECK-LABEL: <t22>:
// CHECK-NEXT: add r4 = r5, r6 // [MIB]{{$}}
// CHECK-NEXT: shr r4 = r5, 3{{$}}
// CHECK-NEXT: nop.b 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MIB]{{$}}
// CHECK-NEXT: nop.i 0{{$}}
// CHECK-NEXT: br.ret.sptk.many b0{{$}}
t23:
	.mlx
	add r4 = r5, r6 ;;
	movl r4 = 5
// CHECK-LABEL: <t23>:
// CHECK-NEXT: add r4 = r5, r6 // [MLX]{{$}}
// CHECK-NEXT: nop.x 0 ;;{{$}}
// CHECK-NEXT: nop.m 0 // [MLX]{{$}}
// CHECK-NEXT: movl r4 = 5{{$}}

// In an MLX bundle, nop, hint and break have X-unit forms.
t24:
	.mlx
	add r4 = r5, r6
	nop 0
// CHECK-LABEL: <t24>:
// CHECK-NEXT: add r4 = r5, r6 // [MLX]{{$}}
// CHECK-NEXT: nop.x 0{{$}}
t25:
	.mlx
	add r4 = r5, r6
	hint 1
// CHECK-LABEL: <t25>:
// CHECK-NEXT: add r4 = r5, r6 // [MLX]{{$}}
// CHECK-NEXT: hint.x 1{{$}}
// The instructions of the bundle may be on the line of its template.
t_line:
	{ .mmi add r4 = r5, r6 ; (p6) ld8 r7 = [r8] ; nop.i 0 ;; }
	.mfb nop.m 1
// CHECK-LABEL: <t_line>:
// CHECK-NEXT: add r4 = r5, r6 // [MMI]{{$}}
// CHECK-NEXT: (p6) ld8 r7 = [r8]{{$}}
// CHECK-NEXT: nop.i 0 ;;
// CHECK-NEXT: nop.m 1 // [MFB]{{$}}
t26:
	.mlx
	nop 1
	break 2
// CHECK-LABEL: <t26>:
// CHECK-NEXT: nop.m 1 // [MLX]{{$}}
// CHECK-NEXT: break.x 2 ;;{{$}}
