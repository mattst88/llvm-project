// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -mrelocation-model static \
// RUN:   -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -mrelocation-model pic \
// RUN:   -pic-level 2 -pic-is-pie -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -mrelocation-model pic \
// RUN:   -pic-level 2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PIC

// There is no copy relocation, and no stub of the linker's has the address of
// a function, so what an executable does not define it reaches through the
// linkage table whatever it was compiled as. What it defines is its own.
// A shared object's own function is its own too, since no -fsemantic-interposition
// says that another may take its place.

// CHECK: @defined = dso_local global i32 1
// CHECK: @declared = external global i32
// CHECK: @hidden = external hidden global i32
// CHECK: define dso_local ptr @address()
// CHECK: declare i32 @function(...)

// PIC: @defined = global i32 1
// PIC: @declared = external global i32
// PIC: define dso_local ptr @address()
// PIC: declare i32 @function(...)

int defined = 1;
extern int declared;
extern int hidden __attribute__((visibility("hidden")));
int function();

void *address(void) {
  defined = declared + hidden + function();
  return function;
}
