// RUN: llvm-mc -triple ia64 -filetype=obj %s -o %t
// RUN: llvm-readelf -S -g -r %t | FileCheck %s

// The unwind table and the unwind information of a section's procedures are
// in sections of their own, named after it, and in its group if it has one.
// The table is linked to the code, so that it goes where the code goes. GNU
// as gives the sections the same names and writes the same contents.

// CHECK:      [ [[#TEXT:]]] .text             PROGBITS
// CHECK:      [ [[#]]] .IA_64.unwind_info PROGBITS     {{[0-9a-f]+}} {{[0-9a-f]+}} 000020 00   A  0   0  8
// CHECK-NEXT: [ [[#]]] .IA_64.unwind     IA_64_UNWIND  {{[0-9a-f]+}} {{[0-9a-f]+}} 000030 00  AL  [[#TEXT]]   0  8
// CHECK:      [ [[#FOO:]]] .text.foo         PROGBITS
// CHECK-NEXT: [ [[#]]] .IA_64.unwind_info.text.foo PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000010 00   A  0   0  8
// CHECK-NEXT: [ [[#]]] .IA_64.unwind.text.foo IA_64_UNWIND {{[0-9a-f]+}} {{[0-9a-f]+}} 000018 00  AL  [[#FOO]]   0  8
// CHECK:      [[[#ONCE:]]] .gnu.linkonce.t.bar PROGBITS
// CHECK-NEXT: [[[#]]] .gnu.linkonce.ia64unwi.bar PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000010 00   A  0   0  8
// CHECK-NEXT: [[[#]]] .gnu.linkonce.ia64unw.bar IA_64_UNWIND {{[0-9a-f]+}} {{[0-9a-f]+}} 000018 00  AL [[#ONCE]]   0  8
// CHECK:      [[[#GRP:]]] .text.grp         PROGBITS
// CHECK-NEXT: [[[#]]] .IA_64.unwind_info.text.grp PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000010 00  AG  0   0  8
// CHECK-NEXT: [[[#]]] .IA_64.unwind.text.grp IA_64_UNWIND {{[0-9a-f]+}} {{[0-9a-f]+}} 000018 00 ALG [[#GRP]]   0  8
// CHECK:      [[[#INIT:]]] .init             PROGBITS
// CHECK-NEXT: [[[#]]] .IA_64.unwind_info.init PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000010 00   A  0   0  8
// CHECK-NEXT: [[[#]]] .IA_64.unwind.init IA_64_UNWIND {{[0-9a-f]+}} {{[0-9a-f]+}} 000018 00  AL [[#INIT]]   0  8
// CHECK:      [[[#OWN:]]] own               PROGBITS
// CHECK-NEXT: [[[#]]] .IA_64.unwind_infoown PROGBITS   {{[0-9a-f]+}} {{[0-9a-f]+}} 000010 00   A  0   0  8
// CHECK-NEXT: [[[#]]] .IA_64.unwindown  IA_64_UNWIND   {{[0-9a-f]+}} {{[0-9a-f]+}} 000018 00  AL [[#OWN]]   0  8

// CHECK:      Relocation section '.rela.IA_64.unwind' at offset {{.*}} contains 6 entries:
// CHECK-NEXT:     Offset
// CHECK-NEXT: 0000000000000000 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 0
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 10
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info + 0
// CHECK-NEXT: 0000000000000018 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 10
// CHECK-NEXT: 0000000000000020 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text + 20
// CHECK-NEXT: 0000000000000028 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info + 10
// CHECK:      Relocation section '.rela.IA_64.unwind.text.foo' at offset {{.*}} contains 3 entries:
// CHECK-NEXT:     Offset
// CHECK-NEXT: 0000000000000000 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text.foo + 0
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .text.foo + 10
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .IA_64.unwind_info.text.foo + 0
// CHECK:      Relocation section '.rela.gnu.linkonce.ia64unw.bar' at offset {{.*}} contains 3 entries:
// CHECK-NEXT:     Offset
// CHECK-NEXT: 0000000000000000 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .gnu.linkonce.t.bar + 0
// CHECK-NEXT: 0000000000000008 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .gnu.linkonce.t.bar + 10
// CHECK-NEXT: 0000000000000010 {{.*}} R_IA64_SEGREL64LSB 0000000000000000 .gnu.linkonce.ia64unwi.bar + 0

// CHECK:      COMDAT group section [{{.*}}] `.group' [sig] contains 4 sections:
// CHECK-NEXT:    [Index]    Name
// CHECK-NEXT:    [   [[#GRP]]]   .text.grp
// CHECK-NEXT:    [   [[#GRP+1]]]   .IA_64.unwind_info.text.grp
// CHECK-NEXT:    [   [[#GRP+2]]]   .IA_64.unwind.text.grp
// CHECK-NEXT:    [   [[#GRP+3]]]   .rela.IA_64.unwind.text.grp

	.text
	.proc a
a:
	.body
	nop 0
	.endp a

	.section .text.foo, "ax", @progbits
	.proc b
b:
	.body
	nop 0
	.endp b

	.section .gnu.linkonce.t.bar, "ax", @progbits
	.proc c
c:
	.body
	nop 0
	.endp c

	.section .text.grp, "axG", @progbits, sig, comdat
	.proc d
d:
	.body
	nop 0
	.endp d

	.section .init, "ax", @progbits
	.proc e
e:
	.body
	nop 0
	.endp e

	.section own, "ax", @progbits
	.proc f
f:
	.body
	nop 0
	.endp f

	.text
	.proc g
g:
	.body
	nop 0
	.endp g
