# REQUIRES: ia64
## A call to a function of another module goes through a stub that loads the
## entry point and the gp from a function descriptor in .IA_64.pltoff, which the
## dynamic loader fills in: with -z now at once, otherwise at the first call,
## through the second half of the stub and the three words that
## DT_IA_64_PLT_RESERVE names.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o b.so -o exe
# RUN: llvm-readelf -S exe | FileCheck %s --check-prefix=SEC
# RUN: llvm-readelf -r -d -x .IA_64.pltoff -x .data exe | FileCheck %s --check-prefix=EXE
# RUN: llvm-objdump -d --no-show-raw-insn exe | FileCheck %s --check-prefix=EXE-DIS

# RUN: ld.lld -z now a.o b.so -o now
# RUN: llvm-readelf -d now | FileCheck %s --check-prefix=NOW

# RUN: ld.lld -shared a.o b.so -o so
# RUN: llvm-readelf -r -x .data so | FileCheck %s --check-prefix=SO
# RUN: llvm-objdump -d --no-show-raw-insn -j .text so | FileCheck %s --check-prefix=SO-DIS

# SEC: .plt          PROGBITS {{.*}} 00  AX  0   0 16
# SEC: .IA_64.pltoff PROGBITS {{.*}} 00 WAp  0   0 16

# NOW: (FLAGS) BIND_NOW

#--- a.s
        .text
        .global _start, def
        .proc _start
_start:
        br.call.sptk.many b0 = ext1 ;;
        br.call.sptk.many b0 = ext2 ;;
        br.call.sptk.many b0 = def ;;
        br.call.sptk.many b0 = local ;;
        br.ret.sptk.many b0 ;;
        .endp _start
        .section .text.def, "ax", @progbits
        .proc def
def:
        br.ret.sptk.many b0 ;;
        .endp def
        .proc local
local:
        br.ret.sptk.many b0 ;;
        .endp local

## A descriptor in place: it is not the address of the function.
        .data
        data16 @iplt(ext1)
        data16 @iplt(def)
        data16 @iplt(local)

#--- b.s
        .text
        .global ext1, ext2
        .proc ext1
ext1:
        br.ret.sptk.many b0 ;;
        .endp ext1
        .proc ext2
ext2:
        br.ret.sptk.many b0 ;;
        .endp ext2

#--- end

# EXE: Dynamic section at offset 0x3b0 contains 17 entries:
# EXE-NEXT: Tag                Type                Name/Value
# EXE-NEXT: 0x0000000000000001 (NEEDED)            Shared library: [b.so]
# EXE-NEXT: 0x0000000000000015 (DEBUG)             0x0
# EXE-NEXT: 0x0000000000000007 (RELA)              0x4000000000000260
# EXE-NEXT: 0x0000000000000008 (RELASZ)            24 (bytes)
# EXE-NEXT: 0x0000000000000009 (RELAENT)           24 (bytes)
# EXE-NEXT: 0x0000000000000017 (JMPREL)            0x4000000000000278
# EXE-NEXT: 0x0000000000000002 (PLTRELSZ)          48 (bytes)
# EXE-NEXT: 0x0000000000000014 (PLTREL)            RELA
# EXE-NEXT: 0x0000000000000003 (PLTGOT)            0x40000000000204f0
# EXE-NEXT: 0x0000000070000000 (IA_64_PLT_RESERVE) 0x40000000000204f0
# EXE-NEXT: 0x0000000000000006 (SYMTAB)            0x40000000000001c8
# EXE-NEXT: 0x000000000000000b (SYMENT)            24 (bytes)
# EXE-NEXT: 0x0000000000000005 (STRTAB)            0x400000000000024c
# EXE-NEXT: 0x000000000000000a (STRSZ)             16 (bytes)
# EXE-NEXT: 0x000000006ffffef5 (GNU_HASH)          0x4000000000000210
# EXE-NEXT: 0x0000000000000004 (HASH)              0x400000000000022c
# EXE-NEXT: 0x0000000000000000 (NULL)              0x0
# EXE: Relocation section '.rela.dyn' at offset 0x260 contains 1 entries:
# EXE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# EXE-NEXT: 40000000000204c0  0000000100000081 R_IA64_IPLTLSB         0000000000000000 ext1 + 0
# EXE: Relocation section '.rela.plt' at offset 0x278 contains 2 entries:
# EXE-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# EXE-NEXT: 4000000000020510  0000000100000081 R_IA64_IPLTLSB         0000000000000000 ext1 + 0
# EXE-NEXT: 4000000000020520  0000000200000081 R_IA64_IPLTLSB         0000000000000000 ext2 + 0
# EXE: Hex dump of section '.data':
# EXE-NEXT: 0x40000000000204c0 00000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x40000000000204d0 00030000 00000040 f0040200 00000040 .......@.......@
# EXE-NEXT: 0x40000000000204e0 10030000 00000040 f0040200 00000040 .......@.......@
# EXE: Hex dump of section '.IA_64.pltoff':
# EXE-NEXT: 0x40000000000204f0 00000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x4000000000020500 00000000 00000000 00000000 00000000 ................
# EXE-NEXT: 0x4000000000020510 70030000 00000040 f0040200 00000040 p......@.......@
# EXE-NEXT: 0x4000000000020520 a0030000 00000040 f0040200 00000040 .......@.......@

# EXE-DIS: Disassembly of section .text:
# EXE-DIS: 40000000000002b0 <_start>:
# EXE-DIS-NEXT: 40000000000002b0:      	br.call.sptk.many	b0 = 0x4000000000000350 // [BBB]
# EXE-DIS-NEXT: 40000000000002b6:      	nop.b	0
# EXE-DIS-NEXT: 40000000000002bc:      	nop.b	0 ;;
# EXE-DIS-NEXT: 40000000000002c0:      	br.call.sptk.many	b0 = 0x4000000000000380 // [BBB]
# EXE-DIS-NEXT: 40000000000002c6:      	nop.b	0
# EXE-DIS-NEXT: 40000000000002cc:      	nop.b	0 ;;
# EXE-DIS-NEXT: 40000000000002d0:      	br.call.sptk.many	b0 = 0x4000000000000300 // [BBB]
# EXE-DIS-NEXT: 40000000000002d6:      	nop.b	0
# EXE-DIS-NEXT: 40000000000002dc:      	nop.b	0 ;;
# EXE-DIS-NEXT: 40000000000002e0:      	nop.m	0               // [MBB]
# EXE-DIS-NEXT: 40000000000002e6:      	br.call.sptk.many	b0 = 0x4000000000000310
# EXE-DIS-NEXT: 40000000000002ec:      	nop.b	0 ;;
# EXE-DIS-NEXT: 40000000000002f0:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 40000000000002f6:      	nop.i	0
# EXE-DIS-NEXT: 40000000000002fc:      	br.ret.sptk.many	b0 ;;
# EXE-DIS: 4000000000000300 <def>:
# EXE-DIS-NEXT: 4000000000000300:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 4000000000000306:      	nop.i	0
# EXE-DIS-NEXT: 400000000000030c:      	br.ret.sptk.many	b0 ;;
# EXE-DIS: 4000000000000310 <local>:
# EXE-DIS-NEXT: 4000000000000310:      	nop.m	0               // [MIB]
# EXE-DIS-NEXT: 4000000000000316:      	nop.i	0
# EXE-DIS-NEXT: 400000000000031c:      	br.ret.sptk.many	b0 ;;
# EXE-DIS: Disassembly of section .plt:
# EXE-DIS: 4000000000000320 <.plt>:
# EXE-DIS-NEXT: 4000000000000320:      	mov	r2 = r14 ;;     // [MMI]
# EXE-DIS-NEXT: 4000000000000326:      	addl	r14 = 0, r2
# EXE-DIS-NEXT: 400000000000032c:      	nop.i	0 ;;
# EXE-DIS-NEXT: 4000000000000330:      	ld8	r16 = [r14], 8 ;; // [MMI]
# EXE-DIS-NEXT: 4000000000000336:      	ld8	r17 = [r14], 8
# EXE-DIS-NEXT: 400000000000033c:      	nop.i	0 ;;
# EXE-DIS-NEXT: 4000000000000340:      	ld8	r1 = [r14]      // [MIB]
# EXE-DIS-NEXT: 4000000000000346:      	mov	b6 = r17
# EXE-DIS-NEXT: 400000000000034c:      	br.cond.sptk.few	b6 ;;
# EXE-DIS-NEXT: 4000000000000350:      	addl	r15 = 32, r1 ;; // [MMI]
# EXE-DIS-NEXT: 4000000000000356:      	ld8	r16 = [r15], 8
# EXE-DIS-NEXT: 400000000000035c:      	mov	r14 = r1 ;;
# EXE-DIS-NEXT: 4000000000000360:      	ld8	r1 = [r15]      // [MIB]
# EXE-DIS-NEXT: 4000000000000366:      	mov	b6 = r16
# EXE-DIS-NEXT: 400000000000036c:      	br.cond.sptk.few	b6 ;;
# EXE-DIS-NEXT: 4000000000000370:      	mov	r15 = 0         // [MIB]
# EXE-DIS-NEXT: 4000000000000376:      	nop.i	0
# EXE-DIS-NEXT: 400000000000037c:      	br.cond.sptk.few	0x4000000000000320 ;;
# EXE-DIS-NEXT: 4000000000000380:      	addl	r15 = 48, r1 ;; // [MMI]
# EXE-DIS-NEXT: 4000000000000386:      	ld8	r16 = [r15], 8
# EXE-DIS-NEXT: 400000000000038c:      	mov	r14 = r1 ;;
# EXE-DIS-NEXT: 4000000000000390:      	ld8	r1 = [r15]      // [MIB]
# EXE-DIS-NEXT: 4000000000000396:      	mov	b6 = r16
# EXE-DIS-NEXT: 400000000000039c:      	br.cond.sptk.few	b6 ;;
# EXE-DIS-NEXT: 40000000000003a0:      	mov	r15 = 1         // [MIB]
# EXE-DIS-NEXT: 40000000000003a6:      	nop.i	0
# EXE-DIS-NEXT: 40000000000003ac:      	br.cond.sptk.few	0x4000000000000320 ;;

# SO: Relocation section '.rela.dyn' at offset 0x2b0 contains 4 entries:
# SO-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# SO-NEXT: 00000000000205c0  000000000000006f R_IA64_REL64LSB                   3c0
# SO-NEXT: 00000000000205c8  000000000000006f R_IA64_REL64LSB                   205d0
# SO-NEXT: 00000000000205a0  0000000100000081 R_IA64_IPLTLSB         0000000000000000 ext1 + 0
# SO-NEXT: 00000000000205b0  0000000400000081 R_IA64_IPLTLSB         00000000000003b0 def + 0
# SO: Relocation section '.rela.plt' at offset 0x310 contains 3 entries:
# SO-NEXT: Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
# SO-NEXT: 00000000000205f0  0000000400000081 R_IA64_IPLTLSB         00000000000003b0 def + 0
# SO-NEXT: 0000000000020600  0000000100000081 R_IA64_IPLTLSB         0000000000000000 ext1 + 0
# SO-NEXT: 0000000000020610  0000000200000081 R_IA64_IPLTLSB         0000000000000000 ext2 + 0
# SO: Hex dump of section '.data':
# SO-NEXT: 0x000205a0 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x000205b0 00000000 00000000 00000000 00000000 ................
# SO-NEXT: 0x000205c0 c0030000 00000000 d0050200 00000000 ................

# SO-DIS: Disassembly of section .text:
# SO-DIS: 0000000000000360 <_start>:
# SO-DIS-NEXT: 360:      	br.call.sptk.many	b0 = 0x430 // [BBB]
# SO-DIS-NEXT: 366:      	nop.b	0
# SO-DIS-NEXT: 36c:      	nop.b	0 ;;
# SO-DIS-NEXT: 370:      	br.call.sptk.many	b0 = 0x460 // [BBB]
# SO-DIS-NEXT: 376:      	nop.b	0
# SO-DIS-NEXT: 37c:      	nop.b	0 ;;
# SO-DIS-NEXT: 380:      	br.call.sptk.many	b0 = 0x400 // [BBB]
# SO-DIS-NEXT: 386:      	nop.b	0
# SO-DIS-NEXT: 38c:      	nop.b	0 ;;
# SO-DIS-NEXT: 390:      	nop.m	0                       // [MBB]
# SO-DIS-NEXT: 396:      	br.call.sptk.many	b0 = 0x3c0
# SO-DIS-NEXT: 39c:      	nop.b	0 ;;
# SO-DIS-NEXT: 3a0:      	nop.m	0                       // [MIB]
# SO-DIS-NEXT: 3a6:      	nop.i	0
# SO-DIS-NEXT: 3ac:      	br.ret.sptk.many	b0 ;;
# SO-DIS: 00000000000003b0 <def>:
# SO-DIS-NEXT: 3b0:      	nop.m	0                       // [MIB]
# SO-DIS-NEXT: 3b6:      	nop.i	0
# SO-DIS-NEXT: 3bc:      	br.ret.sptk.many	b0 ;;
# SO-DIS: 00000000000003c0 <local>:
# SO-DIS-NEXT: 3c0:      	nop.m	0                       // [MIB]
# SO-DIS-NEXT: 3c6:      	nop.i	0
# SO-DIS-NEXT: 3cc:      	br.ret.sptk.many	b0 ;;
