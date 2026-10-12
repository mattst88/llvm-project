; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=ia64-unknown-linux-gnu -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; A parallel compare leaves its two predicates alone unless its condition
; holds, so it adds a condition to the one an ordinary compare put there, and
; two branches that go the same way become one.

declare i32 @g()
declare i32 @h()

; Both conditions: leave unless the first holds, or if the opposite of the
; second does.
define i32 @both(i64 %a, i64 %b) {
; CHECK-LABEL: both:
; CHECK:       cmp.eq p7, p6 = 5, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB0_3 ;;
; CHECK:       br.call.sptk.many b0 = g ;;
; O0-LABEL: both:
; O0-NOT:      or.andcm
; OBJ-LABEL: <both>:
; OBJ:         cmp.eq p7, p6 = 5, r32 ;;
; OBJ:         cmp.eq.or.andcm p6, p7 = 0, r33
entry:
  %cmp = icmp eq i64 %a, 5
  %cmp1 = icmp ne i64 %b, 0
  %and = and i1 %cmp, %cmp1
  br i1 %and, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 0, %entry ]
  ret i32 %r
}

; Either condition.
define i32 @either(i64 %a, i64 %b) {
; CHECK-LABEL: either:
; CHECK:       cmp.lt p7, p6 = -1, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 7, r33
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB1_3 ;;
; CHECK:       br.call.sptk.many b0 = h ;;
; OBJ-LABEL: <either>:
; OBJ:         cmp.eq.or.andcm p6, p7 = 7, r33
entry:
  %cmp = icmp slt i64 %a, 0
  %cmp1 = icmp eq i64 %b, 7
  %or = or i1 %cmp, %cmp1
  br i1 %or, label %then, label %else

then:
  %call = call i32 @g()
  br label %return

else:
  %call2 = call i32 @h()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ %call2, %else ]
  ret i32 %r
}

; The parallel compares of order are against zero, which is as much as
; comparing against one or minus one needs. The ordinary compare takes two
; registers.
define i32 @order(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: order:
; CHECK:       cmp.lt p7, p6 = r32, r33 ;;
; CHECK-NEXT:  cmp.gt.or.andcm p6, p7 = r0, r34
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB2_3 ;;
; OBJ-LABEL: <order>:
; OBJ:         cmp.gt.or.andcm p6, p7 = r0, r34
entry:
  %cmp = icmp slt i64 %a, %b
  %cmp1 = icmp sgt i64 %c, -1
  %and = and i1 %cmp, %cmp1
  br i1 %and, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 1, %entry ]
  ret i32 %r
}

; Each further condition is one more parallel compare.
define i32 @three(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: three:
; CHECK:       cmp.eq p6, p7 = 1, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 2, r33 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 3, r34
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB3_4 ;;
; CHECK:       br.call.sptk.many b0 = h ;;
entry:
  %cmp = icmp eq i64 %a, 1
  %cmp1 = icmp eq i64 %b, 2
  %or = or i1 %cmp, %cmp1
  %cmp2 = icmp eq i64 %c, 3
  %or2 = or i1 %or, %cmp2
  br i1 %or2, label %then, label %else

then:
  %call = call i32 @g()
  br label %return

else:
  %call2 = call i32 @h()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ %call2, %else ]
  ret i32 %r
}

; There is no parallel compare of unsigned order.
define i32 @unsigned(i64 %a, i64 %b) {
; CHECK-LABEL: unsigned:
; CHECK:       cmp.ltu p7, p6 = r32, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB4_3 ;;
; CHECK:       cmp.ltu p6, p7 = 99, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.few .LBB4_3 ;;
entry:
  %cmp = icmp ult i64 %a, %b
  %cmp1 = icmp ult i64 %b, 100
  %and = and i1 %cmp, %cmp1
  br i1 %and, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 1, %entry ]
  ret i32 %r
}

; Nor of floating-point values, but a compare of those can be the ordinary
; one that the parallel compare of integers adds to.
define i32 @float(double %x, i64 %b) {
; CHECK-LABEL: float:
; CHECK:       fcmp.lt p7, p6 = f8, f1 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p6, p7 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dptk.few .LBB5_3 ;;
entry:
  %cmp = fcmp olt double %x, 1.000000e+00
  %cmp1 = icmp eq i64 %b, 0
  %and = and i1 %cmp, %cmp1
  br i1 %and, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 1, %entry ]
  ret i32 %r
}

; A floating-point compare can raise an exception, so it is not made where
; the first condition would have left before it.
define i32 @float_second(i64 %b, double %x) {
; CHECK-LABEL: float_second:
; CHECK-NOT:   or.andcm
; CHECK:       br.ret
entry:
  %cmp1 = icmp eq i64 %b, 0
  %cmp = fcmp olt double %x, 1.000000e+00
  %and = and i1 %cmp1, %cmp
  br i1 %and, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  %r = phi i32 [ %call, %then ], [ 1, %entry ]
  ret i32 %r
}
