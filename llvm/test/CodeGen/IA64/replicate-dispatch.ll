; RUN: llc -mtriple=ia64 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=ia64 -verify-machineinstrs -ia64-replicate-dispatch-min=17 < %s \
; RUN:   | FileCheck %s --check-prefix=NOREP

; An interpreter loop: fetch an opcode, call the hook if one is set, and
; switch on the opcode to a handler that branches back.  Sixteen opcodes is
; below the jump-table threshold, so the dispatch is a compare tree, and each
; of the fifteen handlers and the entry gets a tree of its own, each starting
; with its own fetch.  The hook call stays shared.
;
;   for (;;) {
;     unsigned op = *pc++;
;     if (hook)
;       trace();
;     long *a = &r[op >> 4];
;     switch (op & 15) {
;     case 0: f0(a); break;
;     case 1: f1(a); pc++; break;
;     ...
;     case 15: return *a;
;     }
;   }

; CHECK-LABEL: run:
; CHECK-COUNT-16: ld1
; CHECK-NOT:   ld1
; CHECK:       .endp run

; NOREP-LABEL: run:
; NOREP:       ld1
; NOREP-NOT:   ld1
; NOREP:       .endp run

@hook = external local_unnamed_addr global i32, align 4

define i64 @run(ptr nofree noundef readonly captures(none) %pc, ptr noundef %r) {
entry:
  br label %for.cond

for.cond:
  %pc.addr.0 = phi ptr [ %pc, %entry ], [ %pc.addr.0.be, %for.cond.backedge ]
  %incdec.ptr = getelementptr inbounds nuw i8, ptr %pc.addr.0, i64 1
  %0 = load i8, ptr %pc.addr.0, align 1
  %conv = zext i8 %0 to i32
  %1 = load i32, ptr @hook, align 4
  %tobool.not = icmp eq i32 %1, 0
  br i1 %tobool.not, label %if.end, label %if.then

if.then:
  tail call void @trace()
  br label %if.end

if.end:
  %shr = lshr i32 %conv, 4
  %idxprom = zext nneg i32 %shr to i64
  %arrayidx = getelementptr inbounds nuw [8 x i8], ptr %r, i64 %idxprom
  %and = and i32 %conv, 15
  switch i32 %and, label %default.unreachable41 [
    i32 0, label %sw.bb
    i32 1, label %sw.bb1
    i32 2, label %sw.bb3
    i32 3, label %sw.bb4
    i32 4, label %sw.bb5
    i32 5, label %sw.bb6
    i32 6, label %sw.bb7
    i32 7, label %sw.bb8
    i32 8, label %sw.bb9
    i32 9, label %sw.bb10
    i32 10, label %sw.bb11
    i32 11, label %sw.bb12
    i32 12, label %sw.bb13
    i32 13, label %sw.bb14
    i32 14, label %sw.bb15
    i32 15, label %return
  ]

sw.bb:
  tail call void @f0(ptr noundef %arrayidx)
  br label %for.cond.backedge

for.cond.backedge:
  %pc.addr.0.be = phi ptr [ %incdec.ptr, %sw.bb15 ], [ %incdec.ptr, %sw.bb ], [ %incdec.ptr2, %sw.bb1 ], [ %incdec.ptr, %sw.bb3 ], [ %incdec.ptr, %sw.bb4 ], [ %incdec.ptr, %sw.bb5 ], [ %incdec.ptr, %sw.bb6 ], [ %incdec.ptr, %sw.bb7 ], [ %incdec.ptr, %sw.bb8 ], [ %incdec.ptr, %sw.bb9 ], [ %incdec.ptr, %sw.bb10 ], [ %incdec.ptr, %sw.bb11 ], [ %incdec.ptr, %sw.bb12 ], [ %incdec.ptr, %sw.bb13 ], [ %incdec.ptr, %sw.bb14 ]
  br label %for.cond

sw.bb1:
  tail call void @f1(ptr noundef %arrayidx)
  %incdec.ptr2 = getelementptr inbounds nuw i8, ptr %pc.addr.0, i64 2
  br label %for.cond.backedge

sw.bb3:
  tail call void @f2(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb4:
  tail call void @f3(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb5:
  tail call void @f4(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb6:
  tail call void @f5(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb7:
  tail call void @f6(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb8:
  tail call void @f7(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb9:
  tail call void @f8(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb10:
  tail call void @f9(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb11:
  tail call void @f10(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb12:
  tail call void @f11(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb13:
  tail call void @f12(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb14:
  tail call void @f13(ptr noundef %arrayidx)
  br label %for.cond.backedge

sw.bb15:
  tail call void @f14(ptr noundef %arrayidx)
  br label %for.cond.backedge

default.unreachable41:
  unreachable

return:
  %2 = load i64, ptr %arrayidx, align 8
  ret i64 %2
}

declare void @trace() local_unnamed_addr

declare void @f0(ptr noundef) local_unnamed_addr

declare void @f1(ptr noundef) local_unnamed_addr

declare void @f2(ptr noundef) local_unnamed_addr

declare void @f3(ptr noundef) local_unnamed_addr

declare void @f4(ptr noundef) local_unnamed_addr

declare void @f5(ptr noundef) local_unnamed_addr

declare void @f6(ptr noundef) local_unnamed_addr

declare void @f7(ptr noundef) local_unnamed_addr

declare void @f8(ptr noundef) local_unnamed_addr

declare void @f9(ptr noundef) local_unnamed_addr

declare void @f10(ptr noundef) local_unnamed_addr

declare void @f11(ptr noundef) local_unnamed_addr

declare void @f12(ptr noundef) local_unnamed_addr

declare void @f13(ptr noundef) local_unnamed_addr

declare void @f14(ptr noundef) local_unnamed_addr

