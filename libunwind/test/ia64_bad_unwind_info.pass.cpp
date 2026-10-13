// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The unwind descriptors of a function have a length, and one that is cut
// short has operands beyond it. Ensure that a frame with such descriptors is
// refused and that nothing is read for it from beyond them.

// REQUIRES: target=ia64-{{.+}}-linux-gnu

#undef NDEBUG
#include <assert.h>
#include <libunwind.h>
#include <string.h>

extern "C" void truncated();

static int result;

extern "C" __attribute__((noinline)) void walk() {
  unw_context_t context;
  unw_cursor_t cursor;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);
  for (int frames = 0;; ++frames) {
    char buf[64];
    unw_word_t offset;
    if (unw_get_proc_name(&cursor, buf, sizeof(buf), &offset) == 0 &&
        strcmp(buf, "truncated") == 0)
      break;
    assert(unw_step(&cursor) > 0);
    assert(frames < 8);
  }
  result = unw_step(&cursor);
}

// The descriptors are a prologue of 127 slots and the start of a mask of the
// spills in it, which has two bits a slot and room for five bytes.
__asm__("\t.text\n"
        "\t.global truncated#\n"
        "\t.type truncated#, @function\n"
        "truncated:\n"
        "\talloc r33 = ar.pfs, 0, 3, 0, 0\n"
        "\tmov r32 = b0\n"
        "\tmov r34 = r1\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = walk#\n"
        "\t;;\n"
        "\tmov r1 = r34\n"
        "\tmov ar.pfs = r33\n"
        "\tmov b0 = r32\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n"
        ".Ltruncated_end:\n"
        "\t.size truncated#, . - truncated#\n"
        "\t.section .IA_64.unwind_info, \"a\", @progbits\n"
        "\t.align 8\n"
        ".Ltruncated_info:\n"
        "\tdata8 0x0001000000000001\n"
        "\tdata1 0x60, 0x7f, 0xb8, 0, 0, 0, 0, 0\n"
        "\t.section .IA_64.unwind, \"ao\", @unwind, .text\n"
        "\tdata8 @segrel(truncated#)\n"
        "\tdata8 @segrel(.Ltruncated_end)\n"
        "\tdata8 @segrel(.Ltruncated_info)\n"
        "\t.text\n");

int main(int, char **) {
  truncated();
  assert(result == UNW_EBADFRAME);
  return 0;
}
