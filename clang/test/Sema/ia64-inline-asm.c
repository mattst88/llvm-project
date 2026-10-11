// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify %s

void registers(long x, double d) {
  long y;
  __asm__("mov %0 = %1" : "=r"(y) : "r"(x));
  __asm__("addl %0 = 1, %1" : "=r"(y) : "a"(x));
  __asm__("mov %0 = %1" : "=b"(y) : "r"(x));
  __asm__("fneg %0 = %1" : "=f"(d) : "f"(d));
  __asm__("" : : : "r14", "f6", "p6", "b6", "ar.pfs", "ar.lc", "ar.ccv",
                   "ar.unat", "gp", "sp", "tp", "rp", "in0", "in7", "out0",
                   "out7", "memory");
  __asm__("" : : : "loc0"); // expected-error {{unknown register name 'loc0' in asm}}
  __asm__("" : : : "out8"); // expected-error {{unknown register name 'out8' in asm}}
  __asm__("" : : : "r128"); // expected-error {{unknown register name 'r128' in asm}}
  __asm__("" : : : "f128"); // expected-error {{unknown register name 'f128' in asm}}
  __asm__("" : : : "b8");   // expected-error {{unknown register name 'b8' in asm}}
  __asm__("" : "=c"(y));    // expected-error {{invalid output constraint '=c' in asm}}
}

void immediates(void) {
  __asm__("" : : "I"(8191));
  __asm__("" : : "I"(8192));     // expected-error {{value '8192' out of range for constraint 'I'}}
  __asm__("" : : "J"(-2097152));
  __asm__("" : : "J"(2097152));  // expected-error {{value '2097152' out of range for constraint 'J'}}
  __asm__("" : : "K"(-128));
  __asm__("" : : "K"(128));      // expected-error {{value '128' out of range for constraint 'K'}}
  __asm__("" : : "L"(128));
  __asm__("" : : "L"(-128));     // expected-error {{value '-128' out of range for constraint 'L'}}
  __asm__("" : : "M"(63));
  __asm__("" : : "M"(64));       // expected-error {{value '64' out of range for constraint 'M'}}
  __asm__("" : : "N"(-256));
  __asm__("" : : "N"(256));      // expected-error {{value '256' out of range for constraint 'N'}}
  __asm__("" : : "O"(0));
  __asm__("" : : "O"(1));        // expected-error {{value '1' out of range for constraint 'O'}}
  __asm__("" : : "P"(-1));
  __asm__("" : : "P"(1));        // expected-error {{value '1' out of range for constraint 'P'}}
  __asm__("" : : "R"(4));
  __asm__("" : : "R"(5));        // expected-error {{value '5' out of range for constraint 'R'}}
}

void memory(long *p, double *d) {
  __asm__("ld8 %0 = %1" : "=r"(*p) : "S"(*p));
  __asm__("ldfd %0 = %1" : "=f"(*d) : "Q"(*d));
}
