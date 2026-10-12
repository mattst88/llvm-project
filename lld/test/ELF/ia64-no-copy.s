# REQUIRES: ia64
## The dynamic loader has no copy relocation, and the address of a function is
## that of a descriptor it makes. So what an executable refers to directly and
## a shared object defines is a dynamic relocation of a word that holds the
## address, and anything else is refused.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu bad.s -o bad.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu ro.s -o ro.o
# RUN: llvm-mc -filetype=obj -triple=ia64-unknown-linux-gnu b.s -o b.o
# RUN: ld.lld -shared b.o -soname=b.so -o b.so

# RUN: ld.lld a.o b.so -o exe
# RUN: llvm-readelf -r -s exe | FileCheck %s
# RUN: ld.lld -pie a.o b.so -o pie
# RUN: llvm-readelf -r -s pie | FileCheck %s

# CHECK:      Relocation section '.rela.dyn' at offset {{.*}} contains 3 entries:
# CHECK-NOT:  R_IA64_COPY
# CHECK:      R_IA64_DIR64LSB 0000000000000000 var + 0
# CHECK-NEXT: R_IA64_DIR64LSB 0000000000000000 arr + 8
# CHECK-NEXT: R_IA64_DIR64LSB 0000000000000000 func + 0
## Neither is defined here.
# CHECK:      Symbol table '.dynsym'
# CHECK-DAG:  0000000000000000 0 OBJECT GLOBAL DEFAULT UND var
# CHECK-DAG:  0000000000000000 0 FUNC   GLOBAL DEFAULT UND func

# RUN: not ld.lld bad.o b.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
# RUN: not ld.lld -pie bad.o b.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD

# BAD:      error: relocation R_IA64_IMM64 cannot be used against symbol 'var'; recompile with -fPIC
# BAD-NEXT: >>> defined in b.so
# BAD-NEXT: >>> referenced by bad.o:(.text+0x1)
# BAD:      error: relocation R_IA64_IMM64 cannot be used against symbol 'func'; recompile with -fPIC
# BAD:      error: relocation R_IA64_GPREL22 cannot be used against symbol 'var'; recompile with -fPIC
# BAD:      error: relocation R_IA64_PCREL64LSB cannot be used against symbol 'var'; recompile with -fPIC
# BAD:      error: relocation R_IA64_DIR32LSB cannot be used against symbol 'var'; recompile with -fPIC

## The word has to be one the loader can write to.
# RUN: not ld.lld ro.o b.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=RO
# RUN: ld.lld -z notext ro.o b.so -o notext
# RUN: llvm-readelf -r -d notext | FileCheck %s --check-prefix=NOTEXT

# RO:      error: relocation R_IA64_DIR64LSB against symbol 'var' needs a dynamic relocation in a read-only section
# RO-NEXT: >>> defined in b.so
# RO-NEXT: >>> referenced by ro.o:(.rodata+0x0)

# NOTEXT: (TEXTREL)
# NOTEXT: R_IA64_DIR64LSB 0000000000000000 var + 0

#--- a.s
        .text
        .global _start
_start:
        br.ret.sptk.many b0 ;;
        .data
        data8 var
        data8 arr + 8
        data8 func

#--- bad.s
        .text
        .global _start
_start:
        movl r14 = var ;;
        movl r15 = func ;;
        addl r16 = @gprel(var), r1 ;;
        br.ret.sptk.many b0 ;;
        .data
        data8 @pcrel(var)
        data4 var

#--- ro.s
        .text
        .global _start
_start:
        br.ret.sptk.many b0 ;;
        .section .rodata, "a"
        data8 var

#--- b.s
        .text
        .global func
        .type func, @function
func:
        br.ret.sptk.many b0 ;;
        .data
        .global var, arr
        .type var, @object
        .size var, 8
var:
        data8 3
        .type arr, @object
        .size arr, 16
arr:
        data8 1, 2
