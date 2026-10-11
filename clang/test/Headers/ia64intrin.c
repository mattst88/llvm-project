// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -ffreestanding -fsyntax-only -verify %s
// expected-no-diagnostics

// The header declares nothing, and what a program that includes it goes on
// to use is there.
#include <ia64intrin.h>

int swap(long *p, long o, long n) {
  return __sync_bool_compare_and_swap(p, o, n);
}
