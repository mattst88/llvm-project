# REQUIRES: ia64
## The relocations that need nothing made for them: absolute, IP-relative,
## gp-relative, segment-relative, section-relative and link-time values.

# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu %s -o %t.o
# RUN: ld.lld %t.o --defsym=abs14=0x1234 --defsym=abs22=0x123456 \
# RUN:   --defsym=abs64=0x123456789abcdef0 -o %t
# RUN: llvm-readelf -h -S -l %t | FileCheck %s --check-prefix=ELF
# RUN: llvm-nm %t | FileCheck %s --check-prefix=NM
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s --check-prefix=DIS
# RUN: llvm-readelf -x .data %t | FileCheck %s --check-prefix=DATA

# ELF: Machine: Intel IA-64
# ELF: Entry point address: 0x4000000000000120
# ELF: Flags: 0x10, 64-bit

## The image starts where region 2 does and has 64 KiB pages. Short data is
## the last of the data, and gp points at it.
# ELF: .text  PROGBITS 4000000000000120 000120 000080 00  AX 0 0 16
# ELF: .data  PROGBITS 40000000000101a0 0001a0 000048 00  WA 0 0  8
# ELF: .sdata PROGBITS 40000000000101e8 0001e8 000010 00 WAp 0 0  8
# ELF: LOAD 0x000000 0x4000000000000000 0x4000000000000000 0x0001a0 0x0001a0 R E 0x10000
# ELF: LOAD 0x0001a0 0x40000000000101a0 0x40000000000101a0 0x000058 0x000058 RW  0x10000

        .text
        .global _start
        .proc _start
_start:
        adds r14 = abs14, r2
        addl r15 = abs22, r0 ;;
        movl r16 = abs64 ;;
        addl r14 = @gprel(sd), r1 ;;
        movl r15 = @gprel(far) ;;
        br.call.sptk.many b0 = func ;;
        br.cond.dptk.few func ;;
        mov r14 = ip
        br.ret.sptk.many b0 ;;
        .endp _start

        .section .text.func, "ax", @progbits
        .proc func
func:
        br.ret.sptk.many b0 ;;
        .endp func

        .data
far:
        data8 abs64
        data4 abs22
        data4 @pcrel(far)
        data8 @pcrel(far)
        data8 @segrel(far + 8)
        data8 @secrel(far + 8)
        data4 @segrel(far)
        data4 @secrel(far)
        data8 @gprel(sd)
        data4 @gprel(sd)
        data4 0
        data8 @ltv(far)

        .sdata
        data8 0
sd:
        data8 5

# NM: 40000000000101e8 d __gp
# NM-NEXT: 4000000000000120 T _start
# NM-NEXT: 0000000000001234 A abs14
# NM-NEXT: 0000000000123456 A abs22
# NM-NEXT: 123456789abcdef0 A abs64
# NM-NEXT: 40000000000101a0 d far
# NM-NEXT: 4000000000000190 t func
# NM-NEXT: 40000000000101f0 d sd

# DIS: Disassembly of section .text:
# DIS: 4000000000000120 <_start>:
# DIS-NEXT: 4000000000000120:      	adds	r14 = 4660, r2  // [MII]
# DIS-NEXT: 4000000000000126:      	mov	r15 = 1193046 ;;
# DIS-NEXT: 400000000000012c:      	nop.i	0
# DIS-NEXT: 4000000000000130:      	nop.m	0               // [MLX]
# DIS-NEXT: 4000000000000136:      	movl	r16 = 1311768467463790320 ;;
# DIS-NEXT: 4000000000000140:      	addl	r14 = 8, r1 ;;  // [MMI]
# DIS-NEXT: 4000000000000146:      	nop.m	0
# DIS-NEXT: 400000000000014c:      	nop.i	0
# DIS-NEXT: 4000000000000150:      	nop.m	0               // [MLX]
# DIS-NEXT: 4000000000000156:      	movl	r15 = -72 ;;
# DIS-NEXT: 4000000000000160:      	nop.m	0               // [MBB]
# DIS-NEXT: 4000000000000166:      	br.call.sptk.many	b0 = 0x4000000000000190
# DIS-NEXT: 400000000000016c:      	nop.b	0 ;;
# DIS-NEXT: 4000000000000170:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000000000176:      	nop.i	0
# DIS-NEXT: 400000000000017c:      	br.cond.dptk.few	0x4000000000000190 ;;
# DIS-NEXT: 4000000000000180:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000000000186:      	mov	r14 = ip
# DIS-NEXT: 400000000000018c:      	br.ret.sptk.many	b0 ;;
# DIS: 4000000000000190 <func>:
# DIS-NEXT: 4000000000000190:      	nop.m	0               // [MIB]
# DIS-NEXT: 4000000000000196:      	nop.i	0
# DIS-NEXT: 400000000000019c:      	br.ret.sptk.many	b0 ;;

# DATA: Hex dump of section '.data':
# DATA-NEXT: 0x40000000000101a0 f0debc9a 78563412 56341200 f4ffffff ....xV4.V4......
# DATA-NEXT: 0x40000000000101b0 f0ffffff ffffffff 08000000 00000000 ................
# DATA-NEXT: 0x40000000000101c0 08000000 00000000 00000000 00000000 ................
# DATA-NEXT: 0x40000000000101d0 08000000 00000000 08000000 00000000 ................
# DATA-NEXT: 0x40000000000101e0 a0010100 00000040                   .......@
