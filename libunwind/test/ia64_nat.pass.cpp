// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// A register that a function preserves may hold a NaT, which a load cannot
// read: it is spilled with a bit in ar.unat, or copied to a stacked register
// with a bit in the backing store. Ensure that a frame that is resumed has
// the NaT that it had, and has none that it did not have.

// REQUIRES: target=ia64-{{.+}}-linux-gnu

#undef NDEBUG
#include <assert.h>
#include <libunwind.h>
#include <stdint.h>
#include <string.h>

// Bit 0 of the mode is whether r4 is spilled to memory or kept in a stacked
// register by what holder() calls, and bit 1 whether it holds a NaT. With
// bit 2, r4 is spilled by a function that then saves the ar.unat that has its
// bit, and is still in its prologue when it is unwound through. The
// result is whether r4 was a NaT after the call, in bit 0, and whether it was
// the value it was given instead, in bit 1.
extern "C" uint64_t holder(uint64_t mode);

static bool isIn(unw_cursor_t *cursor, const char *name) {
  char buf[64];
  unw_word_t offset;
  return unw_get_proc_name(cursor, buf, sizeof(buf), &offset) == 0 &&
         strcmp(buf, name) == 0;
}

// Has enough frames above the one that keeps r4 in a stacked register for
// the collection of NaT bits that follows it to be in the backing store.
extern "C" __attribute__((noinline)) uint64_t resume_holder(uint64_t depth) {
  if (depth != 0) {
    volatile uint64_t result = resume_holder(depth - 1);
    return result + 1;
  }
  unw_context_t context;
  unw_cursor_t cursor;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);
  int frames = 0;
  while (!isIn(&cursor, "holder")) {
    assert(unw_step(&cursor) > 0);
    assert(++frames < 200);
  }
  unw_resume(&cursor);
  assert(false && "unw_resume returned");
  return 0;
}

__asm__("\t.text\n"
        "\t.global holder#\n"
        "\t.type holder#, @function\n"
        "\t.proc holder#\n"
        "holder:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r34\n"
        "\talloc r34 = ar.pfs, 1, 4, 1, 0\n"
        "\t.save rp, r33\n"
        "\tmov r33 = b0\n"
        "\t.save.g 0x1, r35\n"
        "\tmov r35 = r4\n"
        "\t.body\n"
        "\tmov r36 = r1\n"
        "\tmovl r4 = 0x1a64600df00d\n"
        "\ttbit.nz p6, p7 = r32, 1\n"
        "\t;;\n"
        "\t(p6) ld8.s r4 = [r0]\n"
        "\ttbit.nz p8, p9 = r32, 0\n"
        "\ttbit.nz p10, p11 = r32, 2\n"
        "\tmov r37 = r32\n"
        "\t;;\n"
        "\t(p10) br.cond.sptk.many .Lsave\n"
        "\t;;\n"
        "\t(p8) br.cond.sptk.many .Lspill\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = keeper#\n"
        "\t;;\n"
        "\tbr.sptk.many .Lresumed\n"
        "\t;;\n"
        ".Lspill:\n"
        "\tbr.call.sptk.many b0 = spiller#\n"
        "\t;;\n"
        "\tbr.sptk.many .Lresumed\n"
        "\t;;\n"
        ".Lsave:\n"
        "\tbr.call.sptk.many b0 = saver#\n"
        "\t;;\n"
        ".Lresumed:\n"
        "\tmov r1 = r36\n"
        "\tmovl r2 = 0x1a64600df00d\n"
        "\tmov r8 = r0\n"
        "\ttnat.nz p6, p7 = r4\n"
        "\t;;\n"
        "\t(p6) mov r8 = 1\n"
        // A comparison with a NaT clears both predicates.
        "\tcmp.eq p8, p9 = r2, r4\n"
        "\t;;\n"
        "\t(p8) mov r8 = 2\n"
        "\tmov r4 = r35\n"
        "\tmov ar.pfs = r34\n"
        "\tmov b0 = r33\n"
        "\t;;\n"
        "\tbr.ret.sptk.many b0\n"
        "\t.endp holder#\n"
        "\t.proc spiller#\n"
        "spiller:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r34\n"
        "\talloc r34 = ar.pfs, 1, 4, 1, 0\n"
        "\t.fframe 32\n"
        "\tadd r12 = -32, r12\n"
        "\t.save rp, r33\n"
        "\tmov r33 = b0\n"
        "\t.save ar.unat, r36\n"
        "\tmov r36 = ar.unat\n"
        "\t;;\n"
        "\tadd r2 = 16, r12\n"
        "\t;;\n"
        "\t.spillsp r4, 16\n"
        "\tst8.spill [r2] = r4\n"
        "\t.body\n"
        "\tmov r35 = r1\n"
        "\tmov r4 = r0\n"
        "\tmov r37 = 100\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = resume_holder#\n"
        "\t;;\n"
        "\tbreak 0\n"
        "\t.endp spiller#\n"
        "\t.proc saver#\n"
        "saver:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r34\n"
        "\talloc r34 = ar.pfs, 1, 4, 1, 0\n"
        "\t.fframe 32\n"
        "\tadd r12 = -32, r12\n"
        "\t.save rp, r33\n"
        "\tmov r33 = b0\n"
        "\t.save ar.unat, r36\n"
        "\tmov r36 = ar.unat\n"
        "\t;;\n"
        "\tadd r2 = 16, r12\n"
        "\tadd r3 = 24, r12\n"
        "\t;;\n"
        "\t.spillsp r4, 16\n"
        "\tst8.spill [r2] = r4\n"
        "\t;;\n"
        "\tmov r14 = ar.unat\n"
        "\t;;\n"
        "\t.savesp @priunat, 24\n"
        "\tst8 [r3] = r14\n"
        "\t;;\n"
        // The bit is nowhere but in memory now.
        "\tmov ar.unat = r0\n"
        "\tmov r35 = r1\n"
        "\tmov r4 = r0\n"
        "\tmov r37 = 100\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = resume_holder#\n"
        "\t;;\n"
        "\tnop 0\n"
        "\t;;\n"
        "\t.body\n"
        "\tbreak 0\n"
        "\t.endp saver#\n"
        "\t.proc keeper#\n"
        "keeper:\n"
        "\t.prologue\n"
        "\t.save ar.pfs, r34\n"
        "\talloc r34 = ar.pfs, 1, 4, 1, 0\n"
        "\t.save rp, r33\n"
        "\tmov r33 = b0\n"
        "\t.save.g 0x1, r36\n"
        "\tmov r36 = r4\n"
        "\t.body\n"
        "\tmov r35 = r1\n"
        "\tmov r4 = r0\n"
        "\tmov r37 = 100\n"
        "\t;;\n"
        "\tbr.call.sptk.many b0 = resume_holder#\n"
        "\t;;\n"
        "\tbreak 0\n"
        "\t.endp keeper#\n");

int main(int, char **) {
  assert(holder(0) == 2);
  assert(holder(1) == 2);
  assert(holder(2) == 1);
  assert(holder(3) == 1);
  assert(holder(4) == 2);
  assert(holder(6) == 1);
  return 0;
}
