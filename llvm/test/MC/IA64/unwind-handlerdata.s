// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WARN
// RUN: llvm-readelf -r -x .IA_64.unwind_info -x .text %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// What a procedure says for the handling of exceptions: its personality
// routine, and the data for that routine, which follows the unwind
// information. GNU as writes the same object for this file.

	.text

// The data alone makes an entry, which says nothing.
	.proc f1
f1:
// ASM:      .handlerdata
// ASM-NEXT: .p2align 3
// ASM-NEXT: data8.ua 4386
// ASM-NEXT: .text
	.handlerdata
	data8 0x1122
	.text
	.endp f1

// A personality routine is two flags and a pointer after the descriptors,
// of which there are none here.
	.proc f2
f2:
// ASM: .personality routine
	.personality routine
	nop 0
	.endp f2

// An entry asked for says nothing either, a plain prologue and body or not.
	.proc f3
f3:
// ASM: .unwentry
	.unwentry
	nop 0
	.endp f3
	.proc f4
f4:
	.prologue
	nop 0
	.body
	nop 0
	.unwentry
	.endp f4

// The procedure ends for the unwinder where its data begins, and not with
// a stop as at ".endp".
	.proc f5
f5:
	.prologue
	.fframe 16
	nop 0
	.body
	nop 0
	.handlerdata
	data4 7
	.text
	nop 0
	nop 0
	nop 0
	.endp f5

// The data comes after the pointer. The code is the section before.
	.proc f6
f6:
	.personality local
	.prologue
	.fframe 16
	nop 0
	.body
	nop 0
	.handlerdata
	data4 8
	.previous
	nop 0
	.endp f6
local:

// ".vframepsp" is another name for ".vframesp".
	.section .text.x,"ax",@progbits
	.proc f7
f7:
	.personality routine
	.prologue
// ASM: .vframesp 16
// WARN: :[[#@LINE+1]]:2: warning: .vframepsp is taken to be .vframesp
	.vframepsp 16
	nop 0
	.handlerdata
	data8.ua @gprel(f7#)
	.section .text.x
	.endp f7

// CHECK:      Relocation section '.rela.IA_64.unwind_info'
// CHECK:      0000000000000018 {{.*}} R_IA64_LTOFF_FPTR64LSB {{.*}} routine + 0
// CHECK-NEXT: 0000000000000058 {{.*}} R_IA64_LTOFF_FPTR64LSB {{.*}} .text + 70

// Where each procedure begins and ends, and its unwind information.
// CHECK:      Relocation section '.rela.IA_64.unwind'
// CHECK:      R_IA64_SEGREL64LSB {{.*}} .text + 0
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 0
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 0
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 0
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 10
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 10
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 10
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 20
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 20
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 20
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 30
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 28
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 30
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 40
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 30
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 50
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .text + 60
// CHECK-NEXT: R_IA64_SEGREL64LSB {{.*}} .IA_64.unwind_info + 48

// CHECK:      Relocation section '.rela.IA_64.unwind_info.text.x'
// CHECK:      0000000000000010 {{.*}} R_IA64_LTOFF_FPTR64LSB {{.*}} routine + 0
// CHECK-NEXT: 0000000000000018 {{.*}} R_IA64_GPREL64LSB {{.*}} .text.x + 0

// The bundles before the data of f5 and of f6 have no stop.
// CHECK:      Hex dump of section '.text':
// CHECK-NEXT: 0x00000000 01000000
// CHECK-NEXT: 0x00000010 01000000
// CHECK-NEXT: 0x00000020 01000000
// CHECK-NEXT: 0x00000030 00000000
// CHECK-NEXT: 0x00000040 01000000
// CHECK-NEXT: 0x00000050 00000000
// CHECK-NEXT: 0x00000060 01000000

// CHECK:      Hex dump of section '.IA_64.unwind_info':
// CHECK-NEXT: 0x00000000 00000000 00000100 22110000 00000000
// CHECK-NEXT: 0x00000010 00000000 03000100 00000000 00000000
// CHECK-NEXT: 0x00000020 00000000 00000100 00000000 00000100
// CHECK-NEXT: 0x00000030 01000000 00000100 01e00001 22000000
// CHECK-NEXT: 0x00000040 07000000 00000000 01000000 03000100
// CHECK-NEXT: 0x00000050 01e00001 22000000 00000000 00000000
// CHECK-NEXT: 0x00000060 08000000
