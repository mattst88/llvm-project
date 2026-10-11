// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -std=gnu89 -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// A function declared without its parameters may take a variable number of
// them, so a call to it is a variadic call all of whose arguments are named.
// That is what tells the backend to pass a floating-point argument both in a
// floating-point register and in its slot.

int unprototyped();
int prototyped(int, double);
int variadic(int, ...);

void call(int i, double d, float f, long double l) {
  // CHECK: call i32 (i32, double, double, x86_fp80, ...) @unprototyped(i32 noundef %{{.*}}, double noundef %{{.*}}, double noundef %{{.*}}, x86_fp80 noundef %{{.*}})
  unprototyped(i, d, f, l);
  // CHECK: call i32 @prototyped(i32 noundef %{{.*}}, double noundef %{{.*}})
  prototyped(i, d);
  // CHECK: call i32 (i32, ...) @variadic(i32 noundef %{{.*}}, double noundef %{{.*}})
  variadic(i, d);
}

// CHECK: declare i32 @unprototyped(...)
