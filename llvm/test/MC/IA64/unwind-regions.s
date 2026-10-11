// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -x .IA_64.unwind_info -r %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM

// ".prologue" and ".body" divide a procedure into regions for the unwinder.
// The unwind information has a header for each: its kind and how many slots
// long it is, three slots to a bundle. A region begins right after the
// instruction before the directive, so that it has the slots left empty
// before its first instruction. GNU as writes the same object for this file.

// Each procedure has an entry in the unwind table: where it begins, where
// it ends and where its unwind information is.
// CHECK:      Relocation section '.rela.IA_64.unwind' at offset {{.*}} contains 30 entries:
// CHECK-NEXT:     Offset
// CHECK-NEXT: 0000000000000000 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 10
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 20
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info + 0
// CHECK-NEXT: 0000000000000018 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 20
// CHECK-NEXT: 0000000000000020 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 40
// CHECK-NEXT: 0000000000000028 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info + 10

// The header of the unwind information is its version, 1, and its length
// in words.
// CHECK:      Hex dump of section '.IA_64.unwind_info':

	.text
// A prologue that says nothing, alone or with a body after it, is what a
// procedure is taken to have, and so is not written.
// ASM:      .proc none
// ASM-NEXT: none:
// ASM-NEXT: .prologue
// ASM-NEXT: nop 0
// ASM-NEXT: .body
	.proc none
none:
	.prologue
	nop 0
	.body
	nop 0
	.endp none

// Short regions are in format R1.
// CHECK-NEXT: 0x00000000 01000000 00000100 01212100 00000000
	.proc r1
r1:
	.prologue
	nop 0
	.body
	nop 0
	.body
	nop 0
	.endp r1

// A prologue with a mask, of rp, ar.pfs, psp and pr, and the first of the
// general registers that they are saved in, is in format R2. The register
// may be just its number.
// CHECK-NEXT: 0x00000010 01000000 00000100 46200120 47ff0223
// ASM:      .prologue 12, 32
// ASM:      .prologue 15, 127
	.proc r2
r2:
	.prologue 12, r32
	nop 0
	.body
	.prologue 15, 127
	nop 0
	nop 0
	.body
	nop 0
	nop 0
	.endp r2

// Regions of 32 slots and more are in format R3.
// CHECK-NEXT: 0x00000020 01000000 00000100 60216120 41042800
	.proc r3
r3:
	.prologue
	.rept 33
	nop 0
	.endr
	.body
	.rept 32
	nop 0
	.endr
	.prologue 2, r4
	.rept 40
	nop 0
	.endr
	.endp r3

// A region need not begin where the procedure does, and may be empty.
// CHECK-NEXT: 0x00000030 01000000 00000100 20220000 00000000
	.proc late
late:
	nop 0
	nop 0
	nop 0
	nop 0
	.body
	.body
	nop 0
	.endp late

// The slots that a bundle has before its first instruction are in the
// region that begins there.
// CHECK-NEXT: 0x00000040 01000000 00000100 22212300 00000000
	.proc lead
lead:
	.body
	nop.i 0
	.body
	nop.i 0
	.body
	nop.m 0
	.endp lead

// The slots left empty after an instruction are in the region that the
// next instruction begins, and a label or a stop in between changes nothing.
// CHECK-NEXT: 0x00000050 01000000 00000100 22232225 00000000
	.proc trail
trail:
	.body
	nop.f 0
	.body
.Llabel:
	nop.f 0
.Lother:
	.body
	nop.m 0 ;;
	.body
	nop.b 0
	.endp trail

// An instruction of two slots is in the second and third of its bundle.
// CHECK-NEXT: 0x00000060 01000000 00000100 21222300 00000000
	.proc long
long:
	.body
	nop.m 0
	.body
	movl r4 = 5
	.body
	movl r5 = 6
	.endp long

// A region with no instruction after it begins at the end.
// CHECK-NEXT: 0x00000070 01000000 00000100 23204301 00000000
	.proc last
last:
	.body
	nop.f 0
.Lend:
	.body
	.prologue 6, r1
	.endp last

// After an alignment a region begins with the bundle of its instruction.
// CHECK-NEXT: 0x00000080 01000000 00000100 26230000 00000000
	.proc aligned
aligned:
	.body
	nop.f 0
	.align 32
	.body
	nop.f 0
	.endp aligned

// A procedure with no unwind directives has no unwind information.
	.proc bare
bare:
	nop 0
	.endp bare

// CHECK-NEXT: 0x00000090 01000000 00000100 23000000 00000000
	.proc only
only:
	.body
	nop 0
	.endp only
