// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -msmall-data-limit 8 \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=EIGHT
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -msmall-data-limit 0 \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=NONE

// The module says how large the largest object that is short data is.

// EIGHT: !{i32 8, !"SmallDataLimit", i32 8}
// NONE:  !{i32 8, !"SmallDataLimit", i32 0}

int x;
