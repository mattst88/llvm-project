; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -debug-only=block-placement < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ON
; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -debug-only=block-placement -enable-ext-tsp-block-placement=0 < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=OFF
; REQUIRES: asserts

; Block placement uses the ext-TSP layout on IA64 unless told not to.

; ON: Applying ext-tsp layout
; OFF-NOT: Applying ext-tsp layout

define i64 @f(i64 %a, i64 %b) {
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %l, label %r

l:
  %x = call i64 @g(i64 %a)
  br label %e

r:
  %y = call i64 @g(i64 %b)
  br label %e

e:
  %p = phi i64 [ %x, %l ], [ %y, %r ]
  ret i64 %p
}

declare i64 @g(i64)
