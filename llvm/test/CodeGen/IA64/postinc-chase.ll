; RUN: llc -mtriple=ia64-unknown-linux-gnu -O2 -verify-machineinstrs < %s | FileCheck %s

; The loop walks a list: the pointer it steps to is loaded through the old
; one, by a load that instruction selection made step its address register by
; 28 for the load at that offset. That step is not the pointer's, so the
; other fields are not given pointers that step by it, and their addresses
; are computed from the node each time.

; CHECK-LABEL: sum:
; CHECK:       .LBB0_[[LOOP:[0-9]+]]:
; CHECK-DAG:     adds [[B:r[0-9]+]] = 16, [[N:r[0-9]+]]
; CHECK-DAG:     adds [[A:r[0-9]+]] = 12, [[N]]
; CHECK-DAG:     adds [[C:r[0-9]+]] = 20, [[N]]
; CHECK-DAG:     adds [[F:r[0-9]+]] = 32, [[N]]
; CHECK-DAG:     ld4 {{r[0-9]+}} = [[[B]]]{{$| }}
; CHECK-DAG:     ld4 {{r[0-9]+}} = [[[A]]]{{$| }}
; CHECK-DAG:     ld4 {{r[0-9]+}} = [[[C]]]{{$| }}
; CHECK-DAG:     ld4 {{r[0-9]+}} = [[[F]]]{{$| }}
; CHECK-DAG:     ld8 [[NEXT:r[0-9]+]] = [[[N]]], 28
; CHECK-DAG:     mov [[N]] = [[NEXT]]
; CHECK:         br.cond.dptk.many .LBB0_[[LOOP]]
define i32 @sum(ptr %n) {
entry:
  %null = icmp eq ptr %n, null
  br i1 %null, label %exit, label %loop

loop:
  %total = phi i32 [ %total.next, %loop ], [ 0, %entry ]
  %node = phi ptr [ %next, %loop ], [ %n, %entry ]
  %b.addr = getelementptr inbounds nuw i8, ptr %node, i64 16
  %b = load i32, ptr %b.addr, align 8
  %a.addr = getelementptr inbounds nuw i8, ptr %node, i64 12
  %a = load i32, ptr %a.addr, align 4
  %f.addr = getelementptr inbounds nuw i8, ptr %node, i64 32
  %f = load i32, ptr %f.addr, align 8
  %c.addr = getelementptr inbounds nuw i8, ptr %node, i64 20
  %c = load i32, ptr %c.addr, align 4
  %e.addr = getelementptr inbounds nuw i8, ptr %node, i64 28
  %e = load i32, ptr %e.addr, align 4
  %t0 = add i32 %b, %total
  %t1 = add i32 %t0, %f
  %t2 = add i32 %a, %c
  %t3 = sub i32 %t1, %t2
  %total.next = add i32 %t3, %e
  %next = load ptr, ptr %node, align 8
  %end = icmp eq ptr %next, null
  br i1 %end, label %exit, label %loop

exit:
  %r = phi i32 [ 0, %entry ], [ %total.next, %loop ]
  ret i32 %r
}
