// RUN: %clang_cc1 -E -dM -triple ia64-unknown-linux-gnu < /dev/null \
// RUN:   | FileCheck %s

// CHECK-DAG: #define _LONGLONG 1
// CHECK-DAG: #define _LP64 1
// CHECK-DAG: #define __BIGGEST_ALIGNMENT__ 16
// CHECK-DAG: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
// CHECK-DAG: #define __FP_FAST_FMA 1
// CHECK-DAG: #define __FP_FAST_FMAF 1
// CHECK-DAG: #define __FP_FAST_FMAL 1
// CHECK-DAG: #define __CLANG_ATOMIC_LLONG_LOCK_FREE 2
// CHECK-DAG: #define __CLANG_ATOMIC_POINTER_LOCK_FREE 2
// CHECK-DAG: #define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1
// CHECK-DAG: #define __INT64_TYPE__ long int
// CHECK-DAG: #define __INTMAX_TYPE__ long int
// CHECK-DAG: #define __INTPTR_TYPE__ long int
// CHECK-DAG: #define __LDBL_DIG__ 18
// CHECK-DAG: #define __LDBL_MANT_DIG__ 64
// CHECK-DAG: #define __LDBL_MAX_EXP__ 16384
// CHECK-DAG: #define __LP64__ 1
// CHECK-DAG: #define __PTRDIFF_TYPE__ long int
// CHECK-DAG: #define __SIZEOF_FLOAT128__ 16
// CHECK-DAG: #define __SIZEOF_INT128__ 16
// CHECK-DAG: #define __SIZEOF_INT__ 4
// CHECK-DAG: #define __SIZEOF_LONG_DOUBLE__ 16
// CHECK-DAG: #define __SIZEOF_LONG__ 8
// CHECK-DAG: #define __SIZEOF_POINTER__ 8
// CHECK-DAG: #define __SIZEOF_WCHAR_T__ 4
// CHECK-DAG: #define __SIZE_TYPE__ long unsigned int
// CHECK-DAG: #define __WCHAR_TYPE__ int
// CHECK-DAG: #define __WINT_TYPE__ unsigned int
// CHECK-DAG: #define __REGISTER_PREFIX__ {{$}}
// CHECK-DAG: #define __ia64 1
// CHECK-DAG: #define __ia64__ 1
// CHECK-DAG: #define __itanium__ 1
// CHECK-DAG: #define __linux__ 1
// CHECK-NOT: __CHAR_UNSIGNED__

// GNU as has no CFI directives for IA-64, and GCC does not say that it has
// them, whatever unwind tables or debug information are asked for and even
// with -fdwarf2-cfi-asm.

// RUN: %clang --target=ia64-unknown-linux-gnu -E -dM %s \
// RUN:   | FileCheck %s --check-prefix=CFI-ASM \
// RUN:       --implicit-check-not=__GCC_HAVE_DWARF2_CFI_ASM
// RUN: %clang --target=ia64-unknown-linux-gnu -g -fasynchronous-unwind-tables \
// RUN:   -fdwarf2-cfi-asm -E -dM %s \
// RUN:   | FileCheck %s --check-prefix=CFI-ASM \
// RUN:       --implicit-check-not=__GCC_HAVE_DWARF2_CFI_ASM
// CFI-ASM: #define __ia64__ 1
