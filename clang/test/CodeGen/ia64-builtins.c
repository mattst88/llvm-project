// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// What an unwinder asks of the register stack engine.

// CHECK-LABEL: define{{.*}} ptr @bsp(
// CHECK: call ptr @llvm.ia64.bsp()
void *bsp(void) { return __builtin_ia64_bsp(); }

// CHECK-LABEL: define{{.*}} void @flush(
// CHECK: call void @llvm.ia64.flushrs()
void flush(void) { __builtin_ia64_flushrs(); }

// The registers a landing pad is given its exception in, by the numbers
// the unwinder has for them.

// CHECK-LABEL: define{{.*}} i32 @eh_data(
// CHECK: ret i32 66
int eh_data(void) {
  return __builtin_eh_return_data_regno(0) +
         __builtin_eh_return_data_regno(1) +
         __builtin_eh_return_data_regno(2) +
         __builtin_eh_return_data_regno(3);
}
