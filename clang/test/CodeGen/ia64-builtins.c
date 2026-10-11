// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// What an unwinder asks of the register stack engine.

// CHECK-LABEL: define{{.*}} ptr @bsp(
// CHECK: call ptr @llvm.ia64.bsp()
void *bsp(void) { return __builtin_ia64_bsp(); }

// CHECK-LABEL: define{{.*}} void @flush(
// CHECK: call void @llvm.ia64.flushrs()
void flush(void) { __builtin_ia64_flushrs(); }
