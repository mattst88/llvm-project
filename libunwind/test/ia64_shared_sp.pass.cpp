// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// A function that keeps everything in stacked registers leaves the stack
// pointer where its caller had it. Ensure that the frame that catches an
// exception is told from one that it called, which has the same stack pointer
// and only something to clean up.

// REQUIRES: target=ia64-{{.+}}-linux-gnu

// The frames have memory of their own when not optimized.
// ADDITIONAL_COMPILE_FLAGS: -O2

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <unwind.h>

static int destroyed;
static uintptr_t cfa[2];

struct Counter {
  ~Counter() { ++destroyed; }
};

extern "C" void inner(int value);
extern "C" int outer(int value);

// The address of a function is that of its descriptor.
static uintptr_t entry(void *function) {
  return *static_cast<uintptr_t *>(function);
}

static _Unwind_Reason_Code record(struct _Unwind_Context *context, void *) {
  uintptr_t start = _Unwind_GetRegionStart(context);
  if (start == entry(reinterpret_cast<void *>(&inner)))
    cfa[0] = _Unwind_GetCFA(context);
  if (start == entry(reinterpret_cast<void *>(&outer)))
    cfa[1] = _Unwind_GetCFA(context);
  return _URC_NO_REASON;
}

__attribute__((noinline)) static void thrower(int value) {
  _Unwind_Backtrace(record, nullptr);
  throw value;
}

extern "C" __attribute__((noinline)) void inner(int value) {
  Counter counter;
  thrower(value);
}

extern "C" __attribute__((noinline)) int outer(int value) {
  try {
    inner(value);
  } catch (int caught) {
    return caught;
  }
  return -1;
}

int main(int, char **) {
  assert(outer(42) == 42);
  assert(destroyed == 1);
  // The test is of nothing unless the two frames share a stack pointer.
  assert(cfa[0] != 0 && cfa[0] == cfa[1]);
  return 0;
}
