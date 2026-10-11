// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s

// A result that cannot be copied bit for bit is in memory whose address is
// the first argument, and not in r8 as the address of any other result in
// memory is. The back end is told so by its being in a register.

struct nontrivial {
  nontrivial();
  nontrivial(const nontrivial &);
  ~nontrivial();
  long a;
};

nontrivial return_nontrivial() { return nontrivial(); }
// CHECK-LABEL: define dso_local void @_Z17return_nontrivialv(ptr dead_on_unwind inreg noalias writable sret(%struct.nontrivial) align 8 %agg.result)

struct big { long a[5]; };
big return_big() { return big(); }
// CHECK-LABEL: define dso_local void @_Z10return_bigv(ptr dead_on_unwind noalias writable sret(%struct.big) align 8 %agg.result)

// An argument that cannot be is the address of a copy the caller made.

void pass_nontrivial(nontrivial n) {}
// CHECK-LABEL: define dso_local void @_Z15pass_nontrivial10nontrivial(ptr {{[^,]*}}%n)

// An empty class is one byte, and has a slot.

struct empty {};
void pass_empty(empty e, int i) {}
// CHECK-LABEL: define dso_local void @_Z10pass_empty5emptyi(i8 %e.coerce, i32 noundef %i)

// The members of a base are members.

struct base { double a; };
struct derived : base { double b; };
void pass_derived(derived d) {}
// CHECK-LABEL: define dso_local void @_Z12pass_derived7derived([2 x double] %d.coerce)

bool narrow_bool(bool b) { return b; }
// CHECK-LABEL: define dso_local noundef i8 @_Z11narrow_boolb(i8 noundef %b.coerce)
