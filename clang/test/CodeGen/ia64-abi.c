// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s

#include <stdarg.h>

// Whoever is given an integer narrower than a register extends it, so
// neither an argument nor a result says that it has been extended. A truth
// value is a byte.

char narrow_char(char c) { return c; }
// CHECK-LABEL: define dso_local i8 @narrow_char(i8 noundef %c)

short narrow_short(short s) { return s; }
// CHECK-LABEL: define dso_local i16 @narrow_short(i16 noundef %s)

unsigned narrow_unsigned(unsigned u) { return u; }
// CHECK-LABEL: define dso_local i32 @narrow_unsigned(i32 noundef %u)

enum e { E0, E1 };
enum e narrow_enum(enum e e) { return e; }
// CHECK-LABEL: define dso_local i32 @narrow_enum(i32 noundef %e)

_Bool narrow_bool(_Bool b) { return b; }
// CHECK-LABEL: define dso_local i8 @narrow_bool(i8 noundef %b.coerce)

// An aggregate is the words that its bytes fill, and so is a vector.

struct one { char c; };
void pass_one(struct one s) {}
// CHECK-LABEL: define dso_local void @pass_one(i8 %s.coerce)

struct three { int a, b, c; };
void pass_three(struct three s) {}
// CHECK-LABEL: define dso_local void @pass_three([2 x i64] %s.coerce)

typedef int v4si __attribute__((vector_size(16)));
void pass_vector(v4si v) {}
// CHECK-LABEL: define dso_local void @pass_vector(i128 noundef %v.coerce)

// One that is aligned to sixteen bytes is pairs of words, which begin at an
// even slot.

struct aligned { __int128 a; long b; };
void pass_aligned(struct aligned s) {}
// CHECK-LABEL: define dso_local void @pass_aligned([2 x i128] %s.coerce)

// An empty one has no slot.

struct empty {};
void pass_empty(struct empty e, int i) {}
// CHECK-LABEL: define dso_local void @pass_empty(i32 noundef %i)

// A large one is its bytes in the same slots, which the back end copies.

struct large { long a[17]; };
void pass_large(struct large s) {}
// CHECK-LABEL: define dso_local void @pass_large(ptr noundef byval(%struct.large) align 8 %s)

struct large_aligned { long a[17]; } __attribute__((aligned(32)));
void pass_large_aligned(struct large_aligned s) {}
// CHECK-LABEL: define dso_local void @pass_large_aligned(ptr noundef byval(%struct.large_aligned) align 16 %0)

// One whose members are all of one floating-point type is an array of them,
// however many there are, and the members may be nested, in arrays or the
// parts of complex numbers.

struct hfa_float { float a, b, c; };
void pass_hfa_float(struct hfa_float s) {}
// CHECK-LABEL: define dso_local void @pass_hfa_float([3 x float] %s.coerce)

struct hfa_nested { struct { double a; } s; double b[2]; _Complex double z; };
void pass_hfa_nested(struct hfa_nested s) {}
// CHECK-LABEL: define dso_local void @pass_hfa_nested([5 x double] %s.coerce)

void pass_complex(_Complex double z) {}
// CHECK-LABEL: define dso_local void @pass_complex([2 x double] noundef %z.coerce)

struct hfa_extended { long double a, b; };
void pass_hfa_extended(struct hfa_extended s) {}
// CHECK-LABEL: define dso_local void @pass_hfa_extended([2 x x86_fp80] alignstack(16) %s.coerce)

struct hfa_nine { double a[9]; };
void pass_hfa_nine(struct hfa_nine s) {}
// CHECK-LABEL: define dso_local void @pass_hfa_nine([9 x double] %s.coerce)

// The array does not say that the aggregate was declared to be aligned to
// sixteen bytes, so an attribute does.

struct hfa_aligned { double a, b; } __attribute__((aligned(16)));
void pass_hfa_aligned(struct hfa_aligned s) {}
// CHECK-LABEL: define dso_local void @pass_hfa_aligned([2 x double] alignstack(16) %s.coerce)

// Quadruple precision is not one of the types, and a bit-field or a member
// of another type means there is no one type.

struct not_hfa_quad { __float128 a; };
void pass_not_hfa_quad(struct not_hfa_quad s) {}
// CHECK-LABEL: define dso_local void @pass_not_hfa_quad(i128 %s.coerce)

struct not_hfa_mixed { float a; double b; };
void pass_not_hfa_mixed(struct not_hfa_mixed s) {}
// CHECK-LABEL: define dso_local void @pass_not_hfa_mixed([2 x i64] %s.coerce)

struct not_hfa_field { double a; int : 0; double b; };
void pass_not_hfa_field(struct not_hfa_field s) {}
// CHECK-LABEL: define dso_local void @pass_not_hfa_field([2 x i64] %s.coerce)

// A result of up to four words is in r8 to r11, and one of up to eight
// members of one floating-point type is in f8 to f15. The rest are in
// memory.

struct three return_three(void) { return (struct three){1, 2, 3}; }
// CHECK-LABEL: define dso_local [2 x i64] @return_three()

struct four_words { long a[4]; };
struct four_words return_four_words(void) { return (struct four_words){}; }
// CHECK-LABEL: define dso_local [4 x i64] @return_four_words()

struct five_words { long a[5]; };
struct five_words return_five_words(void) { return (struct five_words){}; }
// CHECK-LABEL: define dso_local void @return_five_words(ptr dead_on_unwind noalias writable sret(%struct.five_words) align 8 %agg.result)

struct aligned return_aligned(void) { return (struct aligned){}; }
// CHECK-LABEL: define dso_local [4 x i64] @return_aligned()

struct hfa_eight { double a[8]; };
struct hfa_eight return_hfa_eight(void) { return (struct hfa_eight){}; }
// CHECK-LABEL: define dso_local [8 x double] @return_hfa_eight()

struct hfa_nine return_hfa_nine(void) { return (struct hfa_nine){}; }
// CHECK-LABEL: define dso_local void @return_hfa_nine(ptr dead_on_unwind noalias writable sret(%struct.hfa_nine) align 8 %agg.result)

_Complex float return_complex(void) { return 1.0f; }
// CHECK-LABEL: define dso_local [2 x float] @return_complex()

// A union that is transparent is passed as its first member is.

typedef union { int *p; long l; } transparent __attribute__((transparent_union));
void pass_transparent(transparent t) {}
// CHECK-LABEL: define dso_local void @pass_transparent(ptr %t.coerce)

// The arguments with no names are one after another in memory, each in the
// slots it was passed in: eight bytes each, or sixteen and aligned to as
// many.

int va_int(int n, ...) {
// CHECK-LABEL: define dso_local i32 @va_int(i32 noundef %n, ...)
// CHECK:       %argp.cur = load ptr, ptr %ap
// CHECK-NEXT:  %argp.next = getelementptr inbounds i8, ptr %argp.cur, i64 8
// CHECK-NEXT:  store ptr %argp.next, ptr %ap
// CHECK-NEXT:  load i32, ptr %argp.cur
  va_list ap;
  va_start(ap, n);
  int r = va_arg(ap, int);
  va_end(ap);
  return r;
}

long double va_extended(int n, ...) {
// CHECK-LABEL: define dso_local x86_fp80 @va_extended(i32 noundef %n, ...)
// CHECK:       %argp.cur = load ptr, ptr %ap
// CHECK-NEXT:  [[ADD:%.*]] = getelementptr inbounds i8, ptr %argp.cur, i32 15
// CHECK-NEXT:  %argp.cur.aligned = call ptr @llvm.ptrmask.p0.i64(ptr [[ADD]], i64 -16)
// CHECK-NEXT:  %argp.next = getelementptr inbounds i8, ptr %argp.cur.aligned, i64 16
  va_list ap;
  va_start(ap, n);
  long double r = va_arg(ap, long double);
  va_end(ap);
  return r;
}

struct three va_three(int n, ...) {
// CHECK-LABEL: define dso_local [2 x i64] @va_three(i32 noundef %n, ...)
// CHECK:       %argp.cur = load ptr, ptr %ap
// CHECK-NEXT:  %argp.next = getelementptr inbounds i8, ptr %argp.cur, i64 16
  va_list ap;
  va_start(ap, n);
  struct three r = va_arg(ap, struct three);
  va_end(ap);
  return r;
}

// What has no size was not passed, however it is aligned.
struct __attribute__((aligned(16))) empty_aligned {};
long va_empty_aligned(int n, ...) {
// CHECK-LABEL: define dso_local i64 @va_empty_aligned(i32 noundef %n, ...)
// CHECK:       %argp.cur = load ptr, ptr %ap
// CHECK-NEXT:  %argp.next = getelementptr inbounds i8, ptr %argp.cur, i64 0
// CHECK:       %argp.cur1 = load ptr, ptr %ap
// CHECK-NEXT:  %argp.next2 = getelementptr inbounds i8, ptr %argp.cur1, i64 8
  va_list ap;
  va_start(ap, n);
  struct empty_aligned e = va_arg(ap, struct empty_aligned);
  long r = va_arg(ap, long);
  va_end(ap);
  return r;
}
