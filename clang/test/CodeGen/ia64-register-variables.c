// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// The output registers of a function are the last of its frame, and how many
// registers come before them is not known until the function has been
// compiled. They are named by the last eight numbers until then, which is how
// a system call that is written in assembly has its arguments put where the
// kernel looks for them.

long system_call(long number, long a, long b) {
  register long out0 __asm__("out0") = a;
  register long out1 __asm__("out1") = b;
  register long r8 __asm__("r8");
  register long r15 __asm__("r15") = number;
  __asm__ volatile("break 0x100000"
                   : "=r"(r8)
                   : "r"(r15), "r"(out0), "r"(out1)
                   : "out2", "out3", "out4", "out5", "out6", "out7", "ar.pfs");
  return r8;
}
// CHECK-LABEL: define{{.*}} i64 @system_call(
// CHECK: call i64 asm sideeffect "break 0x100000", "={r8},{r15},{r120},{r121},~{r122},~{r123},~{r124},~{r125},~{r126},~{r127},~{ar.pfs}"

long argument(void) {
  register long in1 __asm__("in1");
  __asm__("" : "=r"(in1));
  return in1;
}
// CHECK-LABEL: define{{.*}} i64 @argument(
// CHECK: call i64 asm "", "={r33}"
