// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// CHECK-DAG: @sm = global i64 0, code_model "small"
__attribute__((model(small))) long sm;
// CHECK-DAG: @under = global i64 0, code_model "small"
__attribute__((__model__(__small__))) long under;
// CHECK-DAG: @esm = external global i64, code_model "small"
__attribute__((model(small))) extern long esm;
// CHECK-DAG: @plain = global i64 0, align 8
long plain;

long rd(void) { return sm + under + esm + plain; }
