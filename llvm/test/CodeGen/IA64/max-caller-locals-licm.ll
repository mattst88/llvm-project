; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 < %s | FileCheck %s

; The cap on a calling function's locals (-ia64-max-caller-locals) applies
; only from register allocation on. Before that the function may still turn
; out to rotate registers, and under the cap MachineLICM saw 32 live values
; here as high pressure and left the inner loop's count, n - 1, in the outer
; loop.

; CHECK-LABEL: f:
; CHECK: adds [[LC:r[0-9]+]] = -1, r{{[0-9]+}}
; CHECK: // %loop.preheader
; CHECK-NOT: = -1,
; CHECK: mov ar.lc = [[LC]]
; CHECK: // %loop

declare void @g(i64)

define void @f(ptr %a, ptr %v, ptr %x, ptr %y, i64 %n, i64 %rows) {
entry:
  %p0 = getelementptr i64, ptr %v, i64 0
  %v0 = load volatile i64, ptr %p0, align 8
  %p1 = getelementptr i64, ptr %v, i64 1
  %v1 = load volatile i64, ptr %p1, align 8
  %p2 = getelementptr i64, ptr %v, i64 2
  %v2 = load volatile i64, ptr %p2, align 8
  %p3 = getelementptr i64, ptr %v, i64 3
  %v3 = load volatile i64, ptr %p3, align 8
  %p4 = getelementptr i64, ptr %v, i64 4
  %v4 = load volatile i64, ptr %p4, align 8
  %p5 = getelementptr i64, ptr %v, i64 5
  %v5 = load volatile i64, ptr %p5, align 8
  %p6 = getelementptr i64, ptr %v, i64 6
  %v6 = load volatile i64, ptr %p6, align 8
  %p7 = getelementptr i64, ptr %v, i64 7
  %v7 = load volatile i64, ptr %p7, align 8
  %p8 = getelementptr i64, ptr %v, i64 8
  %v8 = load volatile i64, ptr %p8, align 8
  %p9 = getelementptr i64, ptr %v, i64 9
  %v9 = load volatile i64, ptr %p9, align 8
  %p10 = getelementptr i64, ptr %v, i64 10
  %v10 = load volatile i64, ptr %p10, align 8
  %p11 = getelementptr i64, ptr %v, i64 11
  %v11 = load volatile i64, ptr %p11, align 8
  %p12 = getelementptr i64, ptr %v, i64 12
  %v12 = load volatile i64, ptr %p12, align 8
  %p13 = getelementptr i64, ptr %v, i64 13
  %v13 = load volatile i64, ptr %p13, align 8
  %p14 = getelementptr i64, ptr %v, i64 14
  %v14 = load volatile i64, ptr %p14, align 8
  %p15 = getelementptr i64, ptr %v, i64 15
  %v15 = load volatile i64, ptr %p15, align 8
  %p16 = getelementptr i64, ptr %v, i64 16
  %v16 = load volatile i64, ptr %p16, align 8
  %p17 = getelementptr i64, ptr %v, i64 17
  %v17 = load volatile i64, ptr %p17, align 8
  %p18 = getelementptr i64, ptr %v, i64 18
  %v18 = load volatile i64, ptr %p18, align 8
  %p19 = getelementptr i64, ptr %v, i64 19
  %v19 = load volatile i64, ptr %p19, align 8
  %p20 = getelementptr i64, ptr %v, i64 20
  %v20 = load volatile i64, ptr %p20, align 8
  %p21 = getelementptr i64, ptr %v, i64 21
  %v21 = load volatile i64, ptr %p21, align 8
  %p22 = getelementptr i64, ptr %v, i64 22
  %v22 = load volatile i64, ptr %p22, align 8
  %p23 = getelementptr i64, ptr %v, i64 23
  %v23 = load volatile i64, ptr %p23, align 8
  %p24 = getelementptr i64, ptr %v, i64 24
  %v24 = load volatile i64, ptr %p24, align 8
  %p25 = getelementptr i64, ptr %v, i64 25
  %v25 = load volatile i64, ptr %p25, align 8
  %p26 = getelementptr i64, ptr %v, i64 26
  %v26 = load volatile i64, ptr %p26, align 8
  %p27 = getelementptr i64, ptr %v, i64 27
  %v27 = load volatile i64, ptr %p27, align 8
  %p28 = getelementptr i64, ptr %v, i64 28
  %v28 = load volatile i64, ptr %p28, align 8
  %p29 = getelementptr i64, ptr %v, i64 29
  %v29 = load volatile i64, ptr %p29, align 8
  %p30 = getelementptr i64, ptr %v, i64 30
  %v30 = load volatile i64, ptr %p30, align 8
  %p31 = getelementptr i64, ptr %v, i64 31
  %v31 = load volatile i64, ptr %p31, align 8
  br label %row

row:
  %r = phi i64 [ 0, %entry ], [ %r.next, %next ]
  %pa = getelementptr float, ptr %a, i64 %r
  %t = load float, ptr %pa, align 4
  %z = fcmp oeq float %t, 0.0
  br i1 %z, label %next, label %loop

loop:
  %k = phi i64 [ 0, %row ], [ %k.next, %loop ]
  %px = getelementptr float, ptr %x, i64 %k
  %py = getelementptr float, ptr %y, i64 %k
  %xv = load float, ptr %px, align 4
  %yv = load float, ptr %py, align 4
  %m = fmul float %t, %xv
  %s = fadd float %yv, %m
  store float %s, ptr %py, align 4
  %k.next = add nuw nsw i64 %k, 1
  %e = icmp eq i64 %k.next, %n
  br i1 %e, label %next, label %loop

next:
  %r.next = add i64 %r, 1
  %d = icmp eq i64 %r.next, %rows
  br i1 %d, label %exit, label %row

exit:
  store volatile i64 %v0, ptr %p0, align 8
  store volatile i64 %v1, ptr %p1, align 8
  store volatile i64 %v2, ptr %p2, align 8
  store volatile i64 %v3, ptr %p3, align 8
  store volatile i64 %v4, ptr %p4, align 8
  store volatile i64 %v5, ptr %p5, align 8
  store volatile i64 %v6, ptr %p6, align 8
  store volatile i64 %v7, ptr %p7, align 8
  store volatile i64 %v8, ptr %p8, align 8
  store volatile i64 %v9, ptr %p9, align 8
  store volatile i64 %v10, ptr %p10, align 8
  store volatile i64 %v11, ptr %p11, align 8
  store volatile i64 %v12, ptr %p12, align 8
  store volatile i64 %v13, ptr %p13, align 8
  store volatile i64 %v14, ptr %p14, align 8
  store volatile i64 %v15, ptr %p15, align 8
  store volatile i64 %v16, ptr %p16, align 8
  store volatile i64 %v17, ptr %p17, align 8
  store volatile i64 %v18, ptr %p18, align 8
  store volatile i64 %v19, ptr %p19, align 8
  store volatile i64 %v20, ptr %p20, align 8
  store volatile i64 %v21, ptr %p21, align 8
  store volatile i64 %v22, ptr %p22, align 8
  store volatile i64 %v23, ptr %p23, align 8
  store volatile i64 %v24, ptr %p24, align 8
  store volatile i64 %v25, ptr %p25, align 8
  store volatile i64 %v26, ptr %p26, align 8
  store volatile i64 %v27, ptr %p27, align 8
  store volatile i64 %v28, ptr %p28, align 8
  store volatile i64 %v29, ptr %p29, align 8
  store volatile i64 %v30, ptr %p30, align 8
  store volatile i64 %v31, ptr %p31, align 8
  call void @g(i64 %r.next)
  call void @g(i64 %n)
  ret void
}
