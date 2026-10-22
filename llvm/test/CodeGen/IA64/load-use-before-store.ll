; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -ia64-read-before-store=false \
; RUN:   < %s | FileCheck %s --check-prefix=NOHOIST
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -ia64-stop-after-load-use=false \
; RUN:   < %s | FileCheck %s --check-prefix=NOSTOP

; A store over the byte a load just read cancels the load's fill of L1D
; unless something reads the loaded value in an earlier group. The compare
; moves before the store, and a stop separates them.
define void @mtf(ptr %p, i8 %c, i8 %first) {
; CHECK-LABEL: mtf:
; CHECK:       ld1 [[V:r[0-9]+]] = [{{r[0-9]+}}], 1 ;;
; CHECK-NEXT:  cmp.eq p7, p6 = [[V]], {{r[0-9]+}} ;;
; CHECK:       st1 [r32] = r34, 1{{$}}
; CHECK-NEXT:  mov r34 = [[V]]{{$}}
; CHECK-NEXT:  (p6) br.cond
; NOHOIST-LABEL: mtf:
; NOHOIST:       ld1 [[V:r[0-9]+]] = [{{r[0-9]+}}], 1 ;;
; NOHOIST-NEXT:  st1 [r32] = r34, 1{{$}}
; NOHOIST-NEXT:  mov r34 = [[V]]{{$}}
; NOHOIST-NEXT:  cmp.eq p7, p6 = [[V]], {{r[0-9]+}}{{$}}
; NOSTOP-LABEL:  mtf:
; NOSTOP:        ld1 [[V:r[0-9]+]] = [{{r[0-9]+}}], 1 ;;
; NOSTOP-NEXT:   st1 [r32] = r34, 1{{$}}
; NOSTOP-NEXT:   cmp.eq p7, p6 = [[V]], {{r[0-9]+}}{{$}}
entry:
  br label %loop
loop:
  %a = phi ptr [ %p, %entry ], [ %a.next, %loop ]
  %prev = phi i8 [ %first, %entry ], [ %v, %loop ]
  %v = load i8, ptr %a
  store i8 %prev, ptr %a
  %a.next = getelementptr i8, ptr %a, i64 1
  %done = icmp eq i8 %v, %c
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; A store that only may overwrite the loaded byte keeps its group.
define void @may_alias(ptr %p, ptr %q, i8 %c, i8 %first) {
; CHECK-LABEL: may_alias:
; CHECK:       ld1 [[V:r[0-9]+]] = [r32], 1 ;;
; CHECK-NEXT:  st1 [r33] = r35, 1{{$}}
; CHECK-NEXT:  mov r35 = [[V]]{{$}}
; CHECK-NEXT:  cmp.eq p7, p6 = [[V]], {{r[0-9]+}}{{$}}
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %prev = phi i8 [ %first, %entry ], [ %v, %loop ]
  %a = getelementptr i8, ptr %p, i64 %i
  %b = getelementptr i8, ptr %q, i64 %i
  %v = load i8, ptr %a
  store i8 %prev, ptr %b
  %i.next = add i64 %i, 1
  %done = icmp eq i8 %v, %c
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; The call clobbers the add's result, so the add stays after it.
declare void @f()

define i64 @call(ptr %p, i64 %c) {
; CHECK-LABEL: call:
; CHECK:       ld8 [[V:r[0-9]+]] = [r32]
; CHECK-NEXT:  st8 [r32] = r33
; CHECK:       br.call.sptk.many b0 = f ;;
; CHECK-NEXT:  adds r8 = 1, [[V]]
  %v = load i64, ptr %p
  store i64 %c, ptr %p
  call void @f()
  %r = add i64 %v, 1
  ret i64 %r
}
