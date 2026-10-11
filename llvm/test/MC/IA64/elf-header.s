// RUN: llvm-mc -triple ia64-unknown-linux-gnu -filetype=obj %s \
// RUN:   | llvm-readobj -h -S - | FileCheck %s

// A section with instructions in it is aligned to a bundle.

	.text
	nop.i 0
	.section .text.unaligned,"ax",@progbits
	.byte 0

// CHECK:      Format: elf64-ia64-little
// CHECK-NEXT: Arch: ia64
// CHECK-NEXT: AddressSize: 64bit
// CHECK:        Class: 64-bit (0x2)
// CHECK-NEXT:   DataEncoding: LittleEndian (0x1)
// CHECK:        OS/ABI: SystemV (0x0)
// CHECK:      Type: Relocatable (0x1)
// CHECK-NEXT: Machine: EM_IA_64 (0x32)
// CHECK:      Flags [ (0x10)
// CHECK-NEXT:   EF_IA_64_ABI64 (0x10)
// CHECK-NEXT: ]

// CHECK:      Name: .text ({{.*}})
// CHECK:      Size: 16
// CHECK:      AddressAlignment: 16
// CHECK:      Name: .text.unaligned
// CHECK:      AddressAlignment: 1
