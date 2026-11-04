; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; There are compares for "equal", "less than", "less than or equal" and
; "unordered", and all but the last are false of operands that have no
; order. The rest come of exchanging the operands or the two predicates.

define i64 @oeq(double %a, double %b) {
; CHECK-LABEL: oeq:
; CHECK:       fcmp.eq p6, p7 = f8, f9
; CHECK-NEXT:  mov r8 = r0 ;;
; CHECK-NEXT:  (p6) mov r8 = 1
  %c = fcmp oeq double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @une(double %a, double %b) {
; CHECK-LABEL: une:
; CHECK:       fcmp.eq p7, p6 = f8, f9
  %c = fcmp une double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @olt(float %a, float %b) {
; CHECK-LABEL: olt:
; CHECK:       fcmp.lt p6, p7 = f8, f9
  %c = fcmp olt float %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @uge(double %a, double %b) {
; CHECK-LABEL: uge:
; CHECK:       fcmp.lt p7, p6 = f8, f9
  %c = fcmp uge double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ole(double %a, double %b) {
; CHECK-LABEL: ole:
; CHECK:       fcmp.le p6, p7 = f8, f9
  %c = fcmp ole double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ugt(double %a, double %b) {
; CHECK-LABEL: ugt:
; CHECK:       fcmp.le p7, p6 = f8, f9
  %c = fcmp ugt double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ogt(double %a, double %b) {
; CHECK-LABEL: ogt:
; CHECK:       fcmp.lt p6, p7 = f9, f8
  %c = fcmp ogt double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ule(double %a, double %b) {
; CHECK-LABEL: ule:
; CHECK:       fcmp.lt p7, p6 = f9, f8
  %c = fcmp ule double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @oge(double %a, double %b) {
; CHECK-LABEL: oge:
; CHECK:       fcmp.le p6, p7 = f9, f8
  %c = fcmp oge double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ult(double %a, double %b) {
; CHECK-LABEL: ult:
; CHECK:       fcmp.le p7, p6 = f9, f8
  %c = fcmp ult double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @uno(double %a, double %b) {
; CHECK-LABEL: uno:
; CHECK:       fcmp.unord p6, p7 = f8, f9
  %c = fcmp uno double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ord(double %a, double %b) {
; CHECK-LABEL: ord:
; CHECK:       fcmp.unord p7, p6 = f8, f9
  %c = fcmp ord double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; Of two values that have an order, no one compare says that they differ.
define i64 @one(double %a, double %b) {
; CHECK-LABEL: one:
; CHECK:       fcmp.unord p7, p6 = f8, f9
; CHECK:       fcmp.eq p7, p6 = f8, f9
; CHECK:       and r8 = r15, r14
  %c = fcmp one double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

define i64 @ueq(double %a, double %b) {
; CHECK-LABEL: ueq:
; CHECK:       fcmp.unord p6, p7 = f8, f9
; CHECK:       fcmp.eq p6, p7 = f8, f9
; CHECK:       or r8 = r15, r14
  %c = fcmp ueq double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; A compare that need say nothing of operands without an order is the one
; that is false of them.
define i64 @nnan(double %a, double %b) {
; CHECK-LABEL: nnan:
; CHECK:       fcmp.le p6, p7 = f9, f8
  %c = fcmp nnan oge double %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; f0 is the zero to compare with.
define i64 @zero(double %a) {
; CHECK-LABEL: zero:
; CHECK:       fcmp.eq p6, p7 = f8, f0
  %c = fcmp oeq double %a, 0.0
  %r = zext i1 %c to i64
  ret i64 %r
}

; What is chosen and what chooses are in either kind of register.
define i64 @select_int(double %a, double %b, i64 %x, i64 %y) {
; CHECK-LABEL: select_int:
; CHECK:       fcmp.lt p6, p7 = f8, f9
; CHECK-NEXT:  mov r8 = r35 ;;
; CHECK-NEXT:  (p6) mov r8 = r34
  %c = fcmp olt double %a, %b
  %r = select i1 %c, i64 %x, i64 %y
  ret i64 %r
}

define double @select_fp(double %a, double %b, double %x, double %y) {
; CHECK-LABEL: select_fp:
; CHECK:       fcmp.lt p7, p6 = f8, f9 ;;
; CHECK-NEXT:  (p6) mov f11 = f10 ;;
; CHECK-NEXT:  mov f8 = f11
  %c = fcmp uge double %a, %b
  %r = select i1 %c, double %x, double %y
  ret double %r
}

define double @select_fp_by_int(i64 %a, i64 %b, double %x, double %y) {
; CHECK-LABEL: select_fp_by_int:
; CHECK:       cmp.lt p6, p7 = r32, r33 ;;
; CHECK-NEXT:  (p6) mov f9 = f8 ;;
; CHECK-NEXT:  mov f8 = f9
  %c = icmp slt i64 %a, %b
  %r = select i1 %c, double %x, double %y
  ret double %r
}

define float @select_fp_by_constant(i64 %a, float %x, float %y) {
; CHECK-LABEL: select_fp_by_constant:
; CHECK:       cmp.eq p6, p7 = 5, r32 ;;
; CHECK-NEXT:  (p6) mov f9 = f8 ;;
  %c = icmp eq i64 %a, 5
  %r = select i1 %c, float %x, float %y
  ret float %r
}

declare void @g()

; The branch is taken when the opposite holds, which is not the same
; compare with the operands exchanged: it differs in what it says of
; operands without an order.
define void @branch(double %a, double %b) {
; CHECK-LABEL: branch:
; CHECK:       fcmp.lt p7, p6 = f9, f8{{$}}
; CHECK:       (p6) br.cond.dptk.many .LBB{{[0-9_]+}}{{$}}
; CHECK:       br.call.sptk.many b0 = g ;;
  %c = fcmp ogt double %a, %b
  br i1 %c, label %t, label %f
t:
  call void @g()
  ret void
f:
  ret void
}
