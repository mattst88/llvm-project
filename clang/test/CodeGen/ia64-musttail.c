// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -pic-level 2 -verify \
// RUN:   -fhalf-no-semantic-interposition -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -pic-level 2 -verify \
// RUN:   -DNINE -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -pic-level 2 -verify \
// RUN:   -DODD -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -pic-level 2 -verify \
// RUN:   -DBIG -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -pic-level 2 -DGOOD \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// A tail call leaves the callee to return for its caller, so the callee has
// to find the global pointer and the stack as the caller did. What the
// backend cannot do is said here rather than by the backend. An error ends
// code generation, so each has a run of its own.

#ifdef GOOD

static int local(int a) { return a + 1; }
__attribute__((visibility("hidden"))) int hidden(int);
struct S { long a[4]; };
__attribute__((visibility("hidden"))) struct S hidden_sret(long, long, long,
                                                             long, long, long,
                                                             long, long);

// CHECK-LABEL: define{{.*}} @to_local(
// CHECK: musttail call{{.*}} @local(
int to_local(int a) { __attribute__((musttail)) return local(a); }

// CHECK-LABEL: define{{.*}} @to_hidden(
// CHECK: musttail call{{.*}} @hidden(
int to_hidden(int a) { __attribute__((musttail)) return hidden(a); }

// Where the result goes is not one of the eight slots.
// CHECK-LABEL: define{{.*}} @to_hidden_sret(
// CHECK: musttail call{{.*}} @hidden_sret(
struct S to_hidden_sret(long a, long b, long c, long d, long e, long f,
                        long g, long h) {
  __attribute__((musttail)) return hidden_sret(a, b, c, d, e, f, g, h);
}

// A pointer is to a descriptor that has the callee's global pointer, which
// the backend loads before it branches.
int (*pointer)(int);

// CHECK-LABEL: define{{.*}} @to_pointer(
// CHECK: musttail call{{.*}} %
int to_pointer(int a) { __attribute__((musttail)) return pointer(a); }

// The interpreter CPython builds of tail calls.
__attribute__((preserve_none)) int handler(int);
typedef __attribute__((preserve_none)) int (*Handler)(int);
extern Handler table[];

// CHECK-LABEL: define{{.*}} preserve_nonecc{{.*}} @dispatch(
// CHECK: musttail call preserve_nonecc{{.*}} %
__attribute__((preserve_none)) int dispatch(int op) {
  __attribute__((musttail)) return table[op](op);
}

#elif defined(NINE)

__attribute__((visibility("hidden"))) int nine(long, long, long, long, long,
                                               long, long, long, long);

int to_nine(long a, long b, long c, long d, long e, long f, long g, long h,
            long i) {
  __attribute__((musttail)) return nine(a, b, c, d, e, f, g, h, i); // expected-error {{'musttail' attribute for this call is impossible because calls with arguments on the stack cannot be tail called on IA-64}}
}

#elif defined(ODD)

// The long double is aligned to the eighth slot and is two slots long.
__attribute__((visibility("hidden"))) int odd(long, long, long, long, long,
                                              long, long, long double);

int to_odd(long a, long b, long c, long d, long e, long f, long g,
           long double h) {
  __attribute__((musttail)) return odd(a, b, c, d, e, f, g, h); // expected-error {{'musttail' attribute for this call is impossible because calls with arguments on the stack cannot be tail called on IA-64}}
}

#elif defined(BIG)

struct B { long a[9]; };
__attribute__((visibility("hidden"))) int big(struct B);

int to_big(struct B b) {
  __attribute__((musttail)) return big(b); // expected-error {{'musttail' attribute for this call is impossible because calls with arguments on the stack cannot be tail called on IA-64}}
}

#else

// Whether a function is in this linkage unit is known once the whole file
// has been seen.
int external(int);
int defined(int a) { return a; }

int to_external(int a) {
  __attribute__((musttail)) return external(a); // expected-error {{'musttail' attribute for this call is impossible because calls outside the current linkage unit cannot be tail called on IA-64}}
}

// A definition another module may replace is no more certain to be here.
int to_defined(int a) {
  __attribute__((musttail)) return defined(a); // expected-error {{'musttail' attribute for this call is impossible because calls outside the current linkage unit cannot be tail called on IA-64}}
}

#endif
