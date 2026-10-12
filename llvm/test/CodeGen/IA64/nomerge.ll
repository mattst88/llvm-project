; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; Two calls that are alike are made one, and then neither has a line of its
; own. A call that is marked nomerge is left where it is, which is what a
; sanitizer asks for the calls that report an error.

declare void @bar()
declare void @report(i64) noreturn

define void @merged(i32 %i) nounwind {
; CHECK-LABEL: merged:
; CHECK:       br.call.sptk.many b0 = bar
; CHECK-NOT:   br.call.sptk.many b0 = bar
; CHECK:       br.ret.sptk.many b0
entry:
  switch i32 %i, label %end [
    i32 5, label %a
    i32 7, label %b
  ]

a:
  call void @bar()
  br label %end

b:
  call void @bar()
  br label %end

end:
  ret void
}

define void @kept(i32 %i) nounwind {
; CHECK-LABEL: kept:
; CHECK:       br.call.sptk.many b0 = bar
; CHECK:       br.call.sptk.many b0 = bar
; CHECK:       br.ret.sptk.many b0
entry:
  switch i32 %i, label %end [
    i32 5, label %a
    i32 7, label %b
  ]

a:
  call void @bar() nomerge
  br label %end

b:
  call void @bar() nomerge
  br label %end

end:
  ret void
}

define void @noreturn(i32 %i, i64 %x) nounwind {
; CHECK-LABEL: noreturn:
; CHECK:       br.call.sptk.many b0 = report
; CHECK:       br.call.sptk.many b0 = report
entry:
  switch i32 %i, label %end [
    i32 5, label %a
    i32 7, label %b
  ]

a:
  call void @report(i64 %x) nomerge
  unreachable

b:
  call void @report(i64 %x) nomerge
  unreachable

end:
  ret void
}
