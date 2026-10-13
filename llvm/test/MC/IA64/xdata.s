// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-objdump -s -j .data -j .made %t | FileCheck %s
// RUN: llvm-readobj -r %t | FileCheck %s --check-prefix=REL
// RUN: llvm-objdump -d -j .text %t | FileCheck %s --check-prefix=DIS
// RUN: llvm-readelf -S %t | FileCheck %s --check-prefix=SEC
// RUN: llvm-mc -triple ia64 %s | FileCheck %s --check-prefix=ASM
// RUN: llvm-mc -triple ia64 %s | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2

// ".xdata4 section, values" is "data4 values" written into the section that
// it names, and so for the other data directives. The current section stays
// what it was, and unlike a change of section it leaves the bundle of the
// instructions before it open: the three instructions here are in one, and
// the tag in the table is slot 1 of it. GNU as writes the same sections but for
// the one case that is noted.

	.section __ex_table, "a"
	.previous

	.text
f:
	nop.m 0
	.xdata4 "__ex_table", 1f - ., 2f - .
[1:]	nop.i 1
	.xdata8 .data, f
	nop.i 2
	;;
2:
// Assembly says it as it was said, and not with a change of section and
// one back, which would end the bundle. "." is the place in the section that
// is named.
// ASM:      nop.m 0
// ASM-NEXT: .xdata4 "__ex_table", .Ltmp0-., .Ltmp1-.
// ASM-NEXT: [.Ltmp0:]
// ASM-NEXT: nop.i 1
// ASM-NEXT: .xdata8 ".data", f
// ASM-NEXT: nop.i 2
// ASM:      .xdata2.ua ".data", 22136
// ASM:      .xdata16 ".data", 0x112233445566778899AABBCCDDEEFF00
// ASM-NEXT: .xstring ".data", "ab", "cd"
// ASM-NEXT: .xstringz ".data", "ef\"\\\001"
// ASM-NEXT: .xreal4 ".data", 1.0
// ASM:      .xreal10 ".data", -1.0
// ASM:      .xreal4.ua ".data", 1.0, inf, nan
// ASM:      .xdata16 ".made16", @iplt(f), -1
// ASM:      nop.m 3
// ASM-NEXT: .p2align 3, 0x0
// ASM-NEXT: data8.ua 4

// Each directive aligns as the one it is named after, or not.
	.xdata1 .data, 1, 2
	.xdata2 .data, 0x1234
	.xdata2.ua .data, 0x5678
	.xdata4.ua .data, 9
	.xdata8.ua .data, 10
	.xdata16 .data, 0x112233445566778899aabbccddeeff00
	.xstring .data, "ab", "cd"
	.xstringz .data, "ef\"\\\001"
	.xreal4 .data, 1.0
	.xreal8 .data, 2.0
	.xreal10 .data, -1.0
	.xreal16 .data, 1.0
	.xreal4.ua .data, 1.0, inf, nan
	.XDATA1 .data, 3

// A section that there is not yet is made, as ".section" and its name
// make it: with no flags, or with the type and the flags that the name
// stands for.
	.xdata4 ".made", 5
	.xdata16 .made16, @iplt(f), -1
	.xdata4 .sdata.made, 1
	.xdata4 .sbss.made, 0
	.xdata4 .rodata.made, 1
	.xdata4 .rodatamade, 1
	.xdata4 .data.made, 1
	.xdata4 .bss.made, 0
	.xdata8 .text.made, 1
	.xdata8 .fini, 1
	.xdata8 .init_array, 1
	.xdata4 .tdata.made, 1
	.xdata4 .tbss.made, 0
	.xdata4 .note.made, 1
	.xdata8 .IA_64.unwind.made, 1
	.xdata8 .IA_64.unwind_info.made, 1

// The current section is as good as another, and there it is the plain
// directive, which ends the bundle. GNU as does so without the stop that
// there is here.
	nop.m 3
	.xdata8 .text, 4, 5
	nop.m 6

// DIS:      0: {{.*}} nop.m 0 // [MII]
// DIS-NEXT: 6: {{.*}} nop.i 1
// DIS-NEXT: c: {{.*}} nop.i 2 ;;
// DIS-NEXT: 10: {{.*}} nop.m 3 // [MII]
// DIS:      30: {{.*}} nop.m 6 // [MII]

// REL:      Section ({{.*}}) .rela__ex_table {
// REL-NEXT:   0x0 R_IA64_PCREL32LSB .text 0x1
// REL-NEXT:   0x4 R_IA64_PCREL32LSB .text 0x10
// REL-NEXT: }
// CHECK:      Contents of section .data:
// CHECK-NEXT: 0000 00000000 00000000 01023412 78560900
// CHECK-NEXT: 0010 00000a00 00000000 00000000 00000000
// CHECK-NEXT: 0020 00ffeedd ccbbaa99 88776655 44332211
// CHECK-NEXT: 0030 61626364 6566225c 01000000 0000803f
// CHECK-NEXT: 0040 00000000 00000040 00000000 00000000
// CHECK-NEXT: 0050 00000000 00000080 ffbf0000 00000000
// CHECK-NEXT: 0060 00000000 00000080 ff3f0000 00000000
// CHECK-NEXT: 0070 0000803f 0000807f ffffff7f 03
// CHECK:      Contents of section .made:
// CHECK-NEXT: 0000 05000000

// SEC: [ [[#%d,TEXT:]]] .text PROGBITS
// SEC: __ex_table PROGBITS {{.*}} 000008 00 A 0 0 4
// SEC: .made      PROGBITS {{.*}} 000004 00   0 0 4
// SEC: .sdata.made  PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00 WAp 0 0 4
// SEC: .sbss.made   NOBITS     {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00 WAp 0 0 4
// SEC: .rodata.made PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00   A 0 0 4
// SEC: .rodatamade  PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00     0 0 4
// SEC: .data.made   PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00  WA 0 0 4
// SEC: .bss.made    NOBITS     {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00  WA 0 0 4
// SEC: .text.made   PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 00  AX 0 0 8
// SEC: .fini        PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 00  AX 0 0 8
// SEC: .init_array  INIT_ARRAY {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 08  WA 0 0 8
// SEC: .tdata.made  PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00 WAT 0 0 4
// SEC: .tbss.made   NOBITS     {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00 WAT 0 0 4
// SEC: .note.made   NOTE       {{[0-9a-f]+}} {{[0-9a-f]+}} 000004 00     0 0 4
// SEC: .IA_64.unwind.made IA_64_UNWIND {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 00 L [[#TEXT]] 0 8
// SEC: .IA_64.unwind_info.made PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 00 0 0 8
