; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -stop-after=instruction-select < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=ia64-unknown-linux-gnu -global-isel -global-isel-abort=1 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s

; A call that may unwind is a call between two labels. Where it lands, the
; global pointer is that of the function that threw, and the first thing a
; landing pad does is put back its own.

declare void @may()
declare void @other(ptr)
declare i32 @__gxx_personality_v0(...)

define i32 @caught() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @may() to label %done unwind label %lpad

lpad:
  %l = landingpad { ptr, i32 } catch ptr null
  %e = extractvalue { ptr, i32 } %l, 0
  call void @other(ptr %e)
  br label %done

done:
  %r = phi i32 [ 0, %entry ], [ 1, %lpad ]
  ret i32 %r
}

; MIR-LABEL: name: caught
; MIR: [[GP:%[0-9]+]]:grsave = COPY $r1
; MIR: EH_LABEL
; MIR: CALL @may, [[GP]], csr_ia64_call
; MIR: EH_LABEL
; MIR: BRUNCOND
; MIR: bb.2.lpad (landing-pad):
; MIR: liveins: $r15, $r16
; MIR: EH_LABEL
; MIR-NEXT: $r1 = COPY [[GP]]
; MIR-NEXT: [[EXC:%[0-9]+]]:gr = COPY $r15
; MIR: $r120 = COPY [[EXC]]
; MIR: CALL @other, [[GP]], $r120

; CHECK-LABEL: caught:
; CHECK: mov [[GP:r[0-9]+]] = r1
; CHECK: .Ltmp0:
; CHECK: br.call.sptk.many b0 = may
; CHECK: .Ltmp1:
; CHECK: .Ltmp2:
; CHECK-DAG: mov r1 = [[GP]]
; CHECK-DAG: mov r35 = r15
; CHECK: br.call.sptk.many b0 = other
; CHECK: GCC_except_table0:
