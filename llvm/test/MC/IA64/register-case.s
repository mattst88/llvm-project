// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readobj -r %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: not llvm-mc -triple ia64 --defsym ERR=1 %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR

// A register name is in lower case only, as it is for GNU as: in any other
// case the name is a symbol, and is printed with no '#' after it.

	.text
f:
// CHECK:      0x0 R_IA64_IMM22 R5 0x0
// ASM: mov r4 = R5{{$}}
	mov r4 = R5
// CHECK-NEXT: 0x1 R_IA64_IMM22 Sp 0x0
// ASM: mov r4 = Sp{{$}}
	mov r4 = Sp
// CHECK-NEXT: 0x11 R_IA64_IMM64 AR.LC 0x0
// ASM: movl r4 = AR.LC{{$}}
	movl r4 = AR.LC
// CHECK-NEXT: 0x22 R_IA64_PCREL21B B6 0x0
// ASM: br.call.sptk.many b0 = B6{{$}}
	br.call.sptk.many b0 = B6
// ASM: mov r4 = r5{{$}}
	mov r4 = r5
// ASM: mov r4 = cpuid[r5]
	mov r4 = CPUID[r5]
	;;

	.data
// CHECK:      0x0 R_IA64_DIR64LSB F5 0x0
// CHECK-NEXT: 0x8 R_IA64_DIR64LSB r5 0x0
// CHECK-NEXT: 0x10 R_IA64_DIR64LSB CPUID 0x0
// ASM: data8.ua F5{{$}}
// ASM: data8.ua r5#
// ASM: data8.ua CPUID#
	data8 F5
	data8 r5#
	data8 CPUID#

.ifdef ERR
	.text
// ERR: :[[#@LINE+1]]:3: error: expected a predicate register
	(P6) mov r4 = r5
// ERR: :[[#@LINE+1]]:6: error: invalid operand for instruction
	mov R4 = r5
.endif
