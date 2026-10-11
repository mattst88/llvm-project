// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify \
// RUN:   -DWIDE %s

// A bit-precise integer of up to 128 bits is an integer of that many bits to
// the back end, which passes it as it passes the integer of the next size up.

#ifndef WIDE
_Static_assert(__BITINT_MAXWIDTH__ == 128, "");
_Static_assert(sizeof(_BitInt(65)) == 16, "");
_Static_assert(_Alignof(_BitInt(65)) == 8, "");

// CHECK: define{{.*}} signext i7 @narrow(i7 noundef signext %a)
_BitInt(7) narrow(_BitInt(7) a) { return a; }

// CHECK: define{{.*}} i37 @middle(i37 noundef %a)
unsigned _BitInt(37) middle(unsigned _BitInt(37) a) { return a; }

// CHECK: define{{.*}} i65 @wider(i65 noundef %a)
_BitInt(65) wider(_BitInt(65) a) { return a; }

// CHECK: define{{.*}} i128 @widest(i128 noundef %a)
unsigned _BitInt(128) widest(unsigned _BitInt(128) a) { return a; }
#else
_BitInt(129) too_wide; // expected-error {{signed _BitInt of bit sizes greater than 128 not supported}}
#endif
