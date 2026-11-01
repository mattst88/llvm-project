// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -O1 -emit-llvm -o - %s | FileCheck %s

// Large globals defined here are aligned to 16 bytes. One defined elsewhere,
// or a weak one that may be replaced, keeps its type's alignment: the psABI
// promises nothing more.

// CHECK-DAG: @defined = {{.*}}global [64 x float] {{.*}}, align 16
float defined[64];
// CHECK-DAG: @small = {{.*}}global [2 x float] {{.*}}, align 4
float small[2];
// CHECK-DAG: @weak_def = weak {{.*}}global [64 x float] {{.*}}, align 4
__attribute__((weak)) float weak_def[64];
// CHECK-DAG: @reduced = {{.*}}global [64 x float] {{.*}}, align 16
_Alignas(16) float reduced[64];
// CHECK-DAG: @external = external {{.*}}global [64 x float], align 4
extern float external[64];

float *use(void) { return external; }
// CHECK-LABEL: @load_external(
// CHECK: load float, ptr @external, align 4
float load_external(void) { return external[0]; }
