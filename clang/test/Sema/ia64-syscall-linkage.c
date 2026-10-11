// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=other %s

__attribute__((syscall_linkage)) long f(long); // other-warning {{unknown attribute 'syscall_linkage' ignored}}
__attribute__((syscall_linkage(1))) long g(long); // expected-error {{'syscall_linkage' attribute takes no arguments}} other-warning {{unknown attribute 'syscall_linkage' ignored}}
__attribute__((syscall_linkage)) long v; // expected-warning {{'syscall_linkage' attribute only applies to functions}} other-warning {{unknown attribute 'syscall_linkage' ignored}}
