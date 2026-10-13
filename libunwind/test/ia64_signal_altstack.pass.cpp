// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// A handler on an alternate signal stack has an alternate backing store as
// well, and the registers of what was interrupted that were not yet written
// to the backing store it left are at the start of the new one. Ensure that
// the unwinder finds the frame that was interrupted and the stacked registers
// of its caller.

// REQUIRES: target=ia64-{{.+}}-linux-gnu

#undef NDEBUG
#include <assert.h>
#include <libunwind.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint64_t Magic = 0x1a64600df00dULL;

static bool isIn(unw_cursor_t *cursor, const char *name) {
  char buf[64];
  unw_word_t offset;
  return unw_get_proc_name(cursor, buf, sizeof(buf), &offset) == 0 &&
         strcmp(buf, name) == 0;
}

extern "C" void signal_handler(int) {
  unw_context_t context;
  unw_cursor_t cursor;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  // The frame after that of the trampoline is not at a call.
  int frames = 0;
  while (!unw_is_signal_frame(&cursor)) {
    assert(unw_step(&cursor) > 0);
    assert(++frames < 8);
  }
  assert(frames >= 2);

  // interrupted() keeps its argument, which is its first stacked register,
  // and was nowhere but in the processor.
  while (!isIn(&cursor, "interrupted")) {
    assert(unw_step(&cursor) > 0);
    assert(++frames < 16);
  }
  assert(!unw_is_signal_frame(&cursor));
  unw_word_t value;
  assert(unw_get_reg(&cursor, UNW_IA64_R32, &value) == 0);
  assert(value == Magic);

  assert(unw_step(&cursor) > 0);
  assert(isIn(&cursor, "main"));
  _Exit(0);
}

extern "C" uint64_t interrupted(uint64_t value);
extern "C" void scribble(uint64_t value);

// uint64_t interrupted(uint64_t value) {
//   raise(SIGUSR1);
//   return value;
// }
__asm__("\t.text\n"
        "\t.global interrupted#\n"
        "\t.type interrupted#, @function\n"
        "\t.proc interrupted#\n"
        "interrupted:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r33\n"
        "\talloc r33 = ar.pfs, 1, 3, 2, 0\n"
        "\t.save rp, r34\n"
        "\tmov r34 = b0\n"
        "\t.body\n"
        "\tmov r35 = r1\n"
        "\t;;\n"
        "\tmov r36 = 10\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = raise#\n"
        "\t;;\n"
        "\tmov r1 = r35\n"
        "\tmov r8 = r32\n"
        "\tmov ar.pfs = r33\n"
        "\tmov b0 = r34\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n"
        "\t.endp interrupted#\n"
        // Writes its frame and the arguments of what it calls, which are
        // where those of interrupted() will be, to the backing store.
        "\t.global scribble#\n"
        "\t.type scribble#, @function\n"
        "\t.proc scribble#\n"
        "scribble:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r33\n"
        "\talloc r33 = ar.pfs, 1, 3, 2, 0\n"
        "\t.save rp, r34\n"
        "\tmov r34 = b0\n"
        "\t.body\n"
        "\tmov r36 = r32\n"
        "\tmov r37 = r32\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = scribble_callee#\n"
        "\t;;\n"
        "\tmov ar.pfs = r33\n"
        "\tmov b0 = r34\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n"
        "\t.endp scribble#\n"
        "\t.proc scribble_callee#\n"
        "scribble_callee:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r34\n"
        "\talloc r34 = ar.pfs, 2, 2, 0, 0\n"
        "\t.save rp, r35\n"
        "\tmov r35 = b0\n"
        "\t.body\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = scribble_flush#\n"
        "\t;;\n"
        "\tmov ar.pfs = r34\n"
        "\tmov b0 = r35\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n"
        "\t.endp scribble_callee#\n"
        "scribble_flush:\n"
        "\tflushrs\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n");

int main(int, char **) {
  stack_t stack;
  stack.ss_size = 1 << 18;
  stack.ss_sp = malloc(stack.ss_size);
  stack.ss_flags = 0;
  assert(stack.ss_sp != nullptr);
  assert(sigaltstack(&stack, nullptr) == 0);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;
  action.sa_flags = SA_ONSTACK;
  assert(sigaction(SIGUSR1, &action, nullptr) == 0);

  assert(SIGUSR1 == 10);
  scribble(~Magic);
  interrupted(Magic);
  return 1;
}
