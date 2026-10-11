// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -frounding-math \
// RUN:   -ffp-exception-behavior=strict -emit-llvm -o - %s 2>&1 \
// RUN:   | FileCheck %s

// The rounding mode and the exceptions are the program's to use, and
// nothing is said about overriding the options that ask for them.

// CHECK-NOT: warning:

// CHECK-LABEL: define{{.*}} double @add(
// CHECK: call double @llvm.experimental.constrained.fadd.f64(double %{{.*}}, double %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
double add(double a, double b) { return a + b; }

// CHECK-LABEL: define{{.*}} i32 @less(
// CHECK: call i1 @llvm.experimental.constrained.fcmps.f80(x86_fp80 %{{.*}}, x86_fp80 %{{.*}}, metadata !"olt", metadata !"fpexcept.strict")
int less(long double a, long double b) { return a < b; }

// CHECK-LABEL: define{{.*}} i32 @less_quiet(
// CHECK: call i1 @llvm.experimental.constrained.fcmp.f32(float %{{.*}}, float %{{.*}}, metadata !"olt", metadata !"fpexcept.strict")
int less_quiet(float a, float b) { return __builtin_isless(a, b); }
