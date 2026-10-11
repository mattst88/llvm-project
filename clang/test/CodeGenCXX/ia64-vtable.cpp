// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// A virtual table holds the descriptor of each virtual function, which is
// two pointers long, and so the address of an entry is a pointer to the
// function. Everything before the address point is one pointer long.

struct A {
  virtual int f(int);
  virtual int g(int) = 0;
  virtual ~A();
  int m;
};
struct B : virtual A {
  int f(int) override;
  int g(int) override;
  virtual void h();
};
struct C {
  C();
  virtual void c();
  int n;
};
struct D : C, B {
  int g(int) override;
};

int A::f(int x) { return x; }
A::~A() {}
int B::f(int x) { return x; }
int D::g(int x) { return x; }

// CHECK: @_ZTV1A = constant { [10 x ptr] } { [10 x ptr] [ptr null, ptr @_ZTI1A,
// CHECK-SAME: ptr @_ZN1A1fEi, ptr null, ptr @__cxa_pure_virtual, ptr null,
// CHECK-SAME: ptr @_ZN1AD1Ev, ptr null, ptr @_ZN1AD0Ev, ptr null] }, align 8 #[[TABLE:[0-9]+]]

// A table of two parts, the second of which has thunks.

// CHECK: @_ZTV1B = constant { [13 x ptr], [13 x ptr] } {
// CHECK-SAME: [13 x ptr] [ptr inttoptr (i64 8 to ptr), ptr null, ptr @_ZTI1B,
// CHECK-SAME: ptr @_ZN1B1fEi, ptr null, ptr @_ZN1B1gEi, ptr null, ptr @_ZN1B1hEv, ptr null,
// CHECK-SAME: ptr @_ZN1BD1Ev, ptr null, ptr @_ZN1BD0Ev, ptr null],
// CHECK-SAME: [13 x ptr] [ptr inttoptr (i64 -8 to ptr), ptr inttoptr (i64 -8 to ptr), ptr inttoptr (i64 -8 to ptr), ptr inttoptr (i64 -8 to ptr), ptr @_ZTI1B,
// CHECK-SAME: ptr @_ZTv0_n24_N1B1fEi, ptr null, ptr @_ZTv0_n32_N1B1gEi, ptr null,
// CHECK-SAME: ptr @_ZTv0_n40_N1BD1Ev, ptr null, ptr @_ZTv0_n40_N1BD0Ev, ptr null] }, align 8 #[[TABLE]]

// The address point of a part is where it was, and the part ends later.

// CHECK: @_ZTT1B = unnamed_addr constant [2 x ptr] [ptr getelementptr inbounds inrange(-24, 80) (i8, ptr @_ZTV1B, i64 24),
// CHECK-SAME: ptr getelementptr inbounds inrange(-40, 64) (i8, ptr @_ZTV1B, i64 144)], align 8

// CHECK: @_ZTV1D = constant { [11 x ptr], [13 x ptr], [13 x ptr] } {
// CHECK-SAME: }, align 8 #[[TABLE]]

// The table that a constructor of D has for the B in it.

// CHECK: @_ZTC1D16_1B = constant { [13 x ptr], [13 x ptr] } {
// CHECK-SAME: }, align 8 #[[TABLE]]

// A constructor points the object at the address point.

// CHECK-LABEL: define dso_local void @_ZN1CC2Ev(
// CHECK:         store ptr getelementptr inbounds inrange(-16, 16) (i8, ptr @_ZTV1C, i64 16), ptr %{{.*}}, align 8
void C::c() {}
C::C() {}

// A virtual call does not load from the table.

// CHECK-LABEL: define dso_local noundef i32 @_Z6call_gP1A(
// CHECK:         %vtable = load ptr, ptr %{{.*}}, align 8
// CHECK-NEXT:    %vfn = getelementptr inbounds ptr, ptr %vtable, i64 2
// CHECK-NEXT:    %call = call noundef i32 %vfn(
int call_g(A *a) { return a->g(3); }

// CHECK-LABEL: define dso_local void @_Z7destroyP1A(
// CHECK:         %vfn = getelementptr inbounds ptr, ptr %vtable, i64 6
// CHECK-NEXT:    call void %vfn(
void destroy(A *a) { delete a; }

// A pointer to a virtual member function is one more than the offset of the
// descriptor from the address point.

typedef int (A::*Member)(int);

// CHECK-LABEL: define dso_local [2 x i64] @_Z5firstv(
// CHECK:         store { i64, i64 } { i64 1, i64 0 }, ptr %retval, align 8
Member first() { return &A::f; }

// CHECK-LABEL: define dso_local [2 x i64] @_Z6secondv(
// CHECK:         store { i64, i64 } { i64 17, i64 0 }, ptr %retval, align 8
Member second() { return &A::g; }

// CHECK-LABEL: define dso_local noundef i32 @_Z11call_memberP1AMS_FiiE(
// CHECK:       memptr.virtual:
// CHECK:         %vtable = load ptr, ptr %{{.*}}, align 8
// CHECK-NEXT:    %[[OFFSET:.*]] = sub i64 %memptr.ptr, 1
// CHECK-NEXT:    %memptr.virtualfn = getelementptr i8, ptr %vtable, i64 %[[OFFSET]]
// CHECK-NEXT:    br label %memptr.end
// CHECK:       memptr.end:
// CHECK-NEXT:    %{{.*}} = phi ptr [ %memptr.virtualfn, %memptr.virtual ], [ %memptr.nonvirtualfn, %memptr.nonvirtual ]
int call_member(A *a, Member m) { return (a->*m)(7); }

// CHECK: attributes #[[TABLE]] = { "function-descriptors" }
