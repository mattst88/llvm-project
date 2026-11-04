; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -tail-dup-placement-threshold=2 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -enable-ext-tsp-block-placement=0 -mtriple=ia64-unknown-linux-gnu -tail-dup-placement-threshold=2 \
; RUN:   -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; A parallel compare leaves its two predicates alone unless its condition
; holds, so it adds a condition to the one an ordinary compare put there, and
; two branches that go the same way become one.
; The generic tail duplication threshold keeps a return that two branches
; share; IA64's would copy it into the blocks before it, which leaves the
; branches nothing in common.

declare i32 @g()
declare i32 @h()

; Both conditions: leave unless the first holds, or if the opposite of the
; second does.
define i32 @both(i64 %a, i64 %b) {
; CHECK-LABEL: both:
; CHECK:       mov r8 = r0
; CHECK:       cmp.eq p7, p6 = 5, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dptk.many .LBB0_3{{$}}
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
; CHECK:       mov r34 = r1
; CHECK:       cmp.lt p7, p6 = -1, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 7, r33
; CHECK-NEXT:  (p6) br.cond.dptk.many .LBB1_3{{$}}
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
; CHECK:       mov r8 = 1
; CHECK:       cmp.lt p7, p6 = r32, r33 ;;
; CHECK-NEXT:  cmp.gt.or.andcm p6, p7 = r0, r34
; CHECK-NEXT:  (p6) br.cond.dptk.many .LBB2_3{{$}}
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

; Each further condition is one more parallel compare, in the group of the
; one before.
define i32 @three(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: three:
; CHECK:       cmp.eq p6, p7 = 1, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 2, r33
; CHECK-NEXT:  cmp.eq.or.andcm p6, p7 = 3, r34
; CHECK-NEXT:  (p6) br.cond.dptk.many .LBB3_4{{$}}
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
; CHECK-NEXT:  cmp.ltu p8, p9 = 99, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.many .LBB4_3{{$}}
; CHECK:       (p8) br.cond.dpnt.many .LBB4_3 ;;
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
; CHECK-NEXT:  (p6) br.cond.dptk.many .LBB5_3{{$}}
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

; A branch to a block testing another condition takes that block's branch
; instead, if one of its results goes where the first branch did not. Here
; each condition sends the second block back to the fallthrough of the
; first, so the merged branch is taken only if none of them hold.
define void @target(i64 %a, i64 %b, i64 %c, i64 %d, ptr %p) {
; CHECK-LABEL: target:
; CHECK:       cmp.eq p7, p6 = r32, r33 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = 0, r32
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = r34, r35
; CHECK-NEXT:  (p6) br.cond.dpnt.many .LBB7_4{{$}}
; CHECK:       st8 [r36] = r14
entry:
  %cmp = icmp eq i64 %a, %b
  br i1 %cmp, label %then, label %lor

lor:
  %cmp1 = icmp eq i64 %c, %d
  %cmp2 = icmp eq i64 %a, 0
  %or = or i1 %cmp2, %cmp1
  br i1 %or, label %then, label %end

then:
  store i64 1, ptr %p
  br label %end

end:
  ret void
}

; The second block goes where the first falls through if its condition does
; not hold.
define void @target_both(i64 %a, i64 %b) {
; CHECK-LABEL: target_both:
; CHECK:       cmp.eq p6, p7 = 5, r32 ;;
; CHECK-NEXT:  cmp.eq.or.andcm p7, p6 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.many .LBB8_3{{$}}
; CHECK:       .LBB8_3:
; CHECK-NEXT:  mov r34 = r1
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
entry:
  %cmp = icmp eq i64 %a, 5
  br i1 %cmp, label %next, label %return, !prof !0

next:
  %cmp1 = icmp ne i64 %b, 0
  br i1 %cmp1, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  ret void
}

; Or if its condition does.
define void @target_opposite(i64 %a, i64 %b) {
; CHECK-LABEL: target_opposite:
; CHECK:       cmp.eq p6, p7 = 5, r32 ;;
; CHECK-NEXT:  cmp.ne.or.andcm p7, p6 = 0, r33
; CHECK-NEXT:  (p6) br.cond.dpnt.many .LBB9_3{{$}}
; CHECK:       .LBB9_3:
; CHECK-NEXT:  mov r34 = r1
; CHECK-NEXT:  br.call.sptk.many b0 = g ;;
entry:
  %cmp = icmp eq i64 %a, 5
  br i1 %cmp, label %next, label %return, !prof !0

next:
  %cmp1 = icmp ne i64 %b, 0
  br i1 %cmp1, label %return, label %then

then:
  %call = call i32 @g()
  br label %return

return:
  ret void
}

; The second compare would be made whether or not the first branch went to
; it, which must not happen for one that can raise an exception.
define void @target_float(i64 %a, double %x) {
; CHECK-LABEL: target_float:
; CHECK-NOT:   or.andcm
; CHECK:       fcmp.lt
; CHECK:       .Lfunc_end10:
entry:
  %cmp = icmp eq i64 %a, 5
  br i1 %cmp, label %next, label %return, !prof !0

next:
  %cmp1 = fcmp olt double %x, 1.000000e+00
  br i1 %cmp1, label %then, label %return

then:
  %call = call i32 @g()
  br label %return

return:
  ret void
}

!0 = !{!"branch_weights", i32 1, i32 100}
