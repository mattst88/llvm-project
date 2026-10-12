// RUN: %libomp-compile-and-run
// REQUIRES: ia64-target-arch

// An int is passed in the low half of a register and the high half is
// whatever the caller left there. Ensure that the number of arguments of a
// microtask is taken from the low half only.

#include <stdio.h>

typedef void (*microtask_t)(int *gtid, int *tid, ...);

// Passes its arguments on, with a bit set above the 32 of argc.
int invoke_with_high_bits(microtask_t pkfn, int gtid, int tid, int argc,
                          void **argv, void **exit_frame);

__asm__("\t.text\n"
        "\t.global invoke_with_high_bits#\n"
        "\t.type invoke_with_high_bits#, @function\n"
        "\t.proc invoke_with_high_bits#\n"
        "invoke_with_high_bits:\n"
        "\tmovl r2 = 0x100000000\n"
        "\t;;\n"
        "\tor r35 = r35, r2\n"
        "\tbr.sptk.many __kmp_invoke_microtask#\n"
        "\t.endp invoke_with_high_bits#\n");

static int calls;
static int sum;

static void task0(int *gtid, int *tid) {
  ++calls;
  sum += *gtid + *tid;
}

static void task2(int *gtid, int *tid, int *a, int *b) {
  ++calls;
  sum += *gtid + *tid + *a + *b;
}

int main(void) {
  int a = 100, b = 1000;
  void *argv[2] = {&a, &b};
  void *exit_frame;
  if (invoke_with_high_bits((microtask_t)task0, 1, 2, 0, argv, &exit_frame) !=
      1)
    return 1;
  if (invoke_with_high_bits((microtask_t)task2, 3, 4, 2, argv, &exit_frame) !=
      1)
    return 1;
  if (calls != 2 || sum != 1110) {
    printf("calls %d sum %d\n", calls, sum);
    return 1;
  }
  return 0;
}
