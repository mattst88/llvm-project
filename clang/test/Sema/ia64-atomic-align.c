// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify %s
// expected-no-diagnostics

// An atomic object of sixteen bytes is aligned to sixteen, as GCC aligns
// one, and a smaller one to its size.

struct pair { long a[2]; };
struct holds_pair { char c; _Atomic struct pair p; };
struct holds_complex { char c; _Atomic _Complex double z; };
struct holds_long { char c; _Atomic long l; };

_Static_assert(_Alignof(_Atomic struct pair) == 16, "");
_Static_assert(sizeof(struct holds_pair) == 32, "");
_Static_assert(__builtin_offsetof(struct holds_pair, p) == 16, "");
_Static_assert(sizeof(struct holds_complex) == 32, "");
_Static_assert(__builtin_offsetof(struct holds_complex, z) == 16, "");
_Static_assert(sizeof(struct holds_long) == 16, "");
