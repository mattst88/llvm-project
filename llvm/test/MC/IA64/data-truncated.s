// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WARN --implicit-check-not=warning:
// RUN: llvm-objdump -s -j .data %t | FileCheck %s
// RUN: llvm-mc -triple ia64 %s 2>/dev/null \
// RUN:   | llvm-mc -triple ia64 -filetype=obj -o %t.2
// RUN: cmp %t %t.2
// RUN: not llvm-mc -triple ia64 -filetype=obj --fatal-warnings %s \
// RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=FATAL

// GNU as writes the low bytes of a number that is too wide for its
// directive, and warns of that unless the bits it drops are all those of the
// sign. These are the bytes it writes.

	.data
// WARN: :[[#@LINE+3]]:8: warning: value 0x100 truncated to 0x0
// WARN: :[[#@LINE+2]]:19: warning: value 0x1ff truncated to 0xff
// WARN: :[[#@LINE+1]]:26: warning: value 0xfffffffffffffeff truncated to 0xff
	data1 256, -129, 0x1ff, -257, 0xffffffffffffff80
// FATAL: :[[#@LINE-1]]:8: error: value 0x100 truncated to 0x0
// WARN: :[[#@LINE+1]]:8: warning: value 0x12345 truncated to 0x2345
	data2 0x12345, -32769
// WARN: :[[#@LINE+1]]:8: warning: value 0x123456789 truncated to 0x23456789
	data4 0x123456789
// WARN: :[[#@LINE+1]]:8: warning: value 0x12345 truncated to 0x2345
	.dc.w 0x12345
// WARN: :[[#@LINE+1]]:8: warning: value 0x123456789 truncated to 0x23456789
	.dc.l 0x123456789
// WARN: :[[#@LINE+1]]:9: warning: value 0x54321 truncated to 0x4321
	.short 0x54321
// WARN: :[[#@LINE+1]]:8: warning: value 0x987654321 truncated to 0x87654321
	.long 0x987654321
// WARN: :[[#@LINE+1]]:11: warning: value 0x12345 truncated to 0x2345
	data2.ua 0x12345
	.align 16
// WARN: :[[#@LINE+1]]:8: warning: number truncated to 16 bytes
	.octa 0x112233445566778899aabbccddeeff0011
	.xdata1 .data, 0x1ff
// WARN: :[[#@LINE-1]]:17: warning: value 0x1ff truncated to 0xff

// CHECK:      0000 007fffff 80004523 ff7f0000 89674523
// CHECK-NEXT: 0010 45230000 89674523 21430000 21436587
// CHECK-NEXT: 0020 45230000 00000000 00000000 00000000
// CHECK-NEXT: 0030 1100ffee ddccbbaa 99887766 55443322
// CHECK-NEXT: 0040 ff
