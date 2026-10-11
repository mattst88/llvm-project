// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// CHECK: define{{.*}} i64 @sys_f(i64 noundef %a) [[SYS:#[0-9]+]]
__attribute__((syscall_linkage)) long sys_f(long a) { return a + 1; }

// It is the function's wherever it is next declared.
__attribute__((syscall_linkage)) long sys_g(long a);
// CHECK: define{{.*}} i64 @sys_g(i64 noundef %a) [[SYS]]
long sys_g(long a) { return a; }

// CHECK: define{{.*}} i64 @plain(i64 noundef %a) [[PLAIN:#[0-9]+]]
long plain(long a) { return a; }

// CHECK: attributes [[SYS]] = {{.*}}"syscall-linkage"
// CHECK-NOT: attributes [[PLAIN]] = {{.*}}"syscall-linkage"
