// RUN: %clang_builtins %s %librt -lm -o %t && %run %t
// REQUIRES: librt_has_divxf3

#include "int_lib.h"
#include <fenv.h>
#include <math.h>
#include <stdio.h>

#if HAS_80_BIT_LONG_DOUBLE

#include "fp_test.h"

COMPILER_RT_ABI xf_float __divxf3(xf_float a, xf_float b);

struct rep {
  uint16_t hi;
  uint64_t lo;
};

static const int modes[4] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD,
                             FE_TOWARDZERO};

// A dividend, a divisor, and the quotient in each of the rounding modes.
static const struct {
  struct rep a, b, q[4];
} cases[] = {
    // 1 / 3
    {{0x3fff, UINT64_C(0x8000000000000000)},
     {0x4000, UINT64_C(0xc000000000000000)},
     {{0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaab)},
      {0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaaa)},
      {0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaab)},
      {0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaaa)}}},
    // 2 / 3
    {{0x4000, UINT64_C(0x8000000000000000)},
     {0x4000, UINT64_C(0xc000000000000000)},
     {{0x3ffe, UINT64_C(0xaaaaaaaaaaaaaaab)},
      {0x3ffe, UINT64_C(0xaaaaaaaaaaaaaaaa)},
      {0x3ffe, UINT64_C(0xaaaaaaaaaaaaaaab)},
      {0x3ffe, UINT64_C(0xaaaaaaaaaaaaaaaa)}}},
    // 6 / 3, which is exact
    {{0x4001, UINT64_C(0xc000000000000000)},
     {0x4000, UINT64_C(0xc000000000000000)},
     {{0x4000, UINT64_C(0x8000000000000000)},
      {0x4000, UINT64_C(0x8000000000000000)},
      {0x4000, UINT64_C(0x8000000000000000)},
      {0x4000, UINT64_C(0x8000000000000000)}}},
    // -10 / 7
    {{0xc002, UINT64_C(0xa000000000000000)},
     {0x4001, UINT64_C(0xe000000000000000)},
     {{0xbfff, UINT64_C(0xb6db6db6db6db6db)},
      {0xbfff, UINT64_C(0xb6db6db6db6db6dc)},
      {0xbfff, UINT64_C(0xb6db6db6db6db6db)},
      {0xbfff, UINT64_C(0xb6db6db6db6db6db)}}},
    // The largest significand by the smallest but one, and the reverse.
    {{0x3fff, UINT64_C(0xffffffffffffffff)},
     {0x3fff, UINT64_C(0x8000000000000001)},
     {{0x3fff, UINT64_C(0xfffffffffffffffd)},
      {0x3fff, UINT64_C(0xfffffffffffffffd)},
      {0x3fff, UINT64_C(0xfffffffffffffffe)},
      {0x3fff, UINT64_C(0xfffffffffffffffd)}}},
    {{0x3fff, UINT64_C(0x8000000000000001)},
     {0x3fff, UINT64_C(0xffffffffffffffff)},
     {{0x3ffe, UINT64_C(0x8000000000000002)},
      {0x3ffe, UINT64_C(0x8000000000000001)},
      {0x3ffe, UINT64_C(0x8000000000000002)},
      {0x3ffe, UINT64_C(0x8000000000000001)}}},
    {{0x4005, UINT64_C(0xc90fdaa22168c235)},
     {0xbffd, UINT64_C(0xadf85458a2bb4a9b)},
     {{0xc007, UINT64_C(0x93eedfb138edef7f)},
      {0xc007, UINT64_C(0x93eedfb138edef7f)},
      {0xc007, UINT64_C(0x93eedfb138edef7e)},
      {0xc007, UINT64_C(0x93eedfb138edef7e)}}},
    // Operands at the top of the range of exponents.
    {{0x7ffe, UINT64_C(0xffffffffffffffff)},
     {0x7ffd, UINT64_C(0x8000000000000001)},
     {{0x4000, UINT64_C(0xfffffffffffffffd)},
      {0x4000, UINT64_C(0xfffffffffffffffd)},
      {0x4000, UINT64_C(0xfffffffffffffffe)},
      {0x4000, UINT64_C(0xfffffffffffffffd)}}},
};

static int test__divxf3(xf_float a, xf_float b, uint16_t expectedHi,
                        uint64_t expectedLo) {
  xf_float x = __divxf3(a, b);
  int ret = compareResultF80(x, expectedHi, expectedLo);
  if (ret)
    printf("error in __divxf3(%La, %La) = %La, expected %La\n", a, b, x,
           F80FromRep80(expectedHi, expectedLo));
  return ret;
}

#endif

int main(void) {
#if HAS_80_BIT_LONG_DOUBLE
  for (unsigned m = 0; m != 4; ++m) {
    fesetround(modes[m]);
    for (unsigned i = 0; i != sizeof(cases) / sizeof(cases[0]); ++i)
      if (test__divxf3(F80FromRep80(cases[i].a.hi, cases[i].a.lo),
                       F80FromRep80(cases[i].b.hi, cases[i].b.lo),
                       cases[i].q[m].hi, cases[i].q[m].lo))
        return 1;
  }
  fesetround(FE_TONEAREST);

  const xf_float one = F80FromRep80(0x3fff, UINT64_C(0x8000000000000000));
  const xf_float zero = F80FromRep80(0, 0);
  const xf_float max = F80FromRep80(0x7ffe, UINT64_C(0xffffffffffffffff));
  // A zero divisor, a zero dividend, and a quotient that is out of range.
  if (test__divxf3(one, zero, 0x7fff, UINT64_C(0x8000000000000000)))
    return 1;
  if (test__divxf3(-one, zero, 0xffff, UINT64_C(0x8000000000000000)))
    return 1;
  if (test__divxf3(zero, -one, 0x8000, 0))
    return 1;
  if (test__divxf3(max, F80FromRep80(0x3ffe, UINT64_C(0x8000000000000000)),
                   0x7fff, UINT64_C(0x8000000000000000)))
    return 1;
  // An infinity.
  if (test__divxf3(makeInf80(), -one, 0xffff, UINT64_C(0x8000000000000000)))
    return 1;
  if (test__divxf3(one, makeInf80(), 0, 0))
    return 1;
  // What has no quotient.
  if (!isnan(__divxf3(zero, zero)) ||
      !isnan(__divxf3(makeInf80(), makeInf80())) ||
      !isnan(__divxf3(makeQNaN80(), one)) ||
      !isnan(__divxf3(one, makeQNaN80())))
    return 1;
#else
  printf("skipped\n");
#endif
  return 0;
}
