; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs \
; RUN:   -ia64-postinc-chain-gap=0 < %s | FileCheck %s --check-prefix=OFF

; Each word of the key goes into a long chain of xors that it joins late, so
; it can wait for the load before it, and four loads take one add.
define i64 @late_words(ptr %key, i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: late_words:
; CHECK:       adds [[P:r[0-9]+]] = 256, r32
; CHECK-NOT:   adds
; CHECK:       ld8 r{{[0-9]+}} = [[[P]]], 8
; CHECK:       ld8 r{{[0-9]+}} = [[[P]]], 8
; CHECK:       ld8 r{{[0-9]+}} = [[[P]]], 8
; CHECK:       ld8 r{{[0-9]+}} = [[[P]]] ;;
; OFF-LABEL:   late_words:
; OFF-COUNT-4: adds r{{[0-9]+}} = {{[0-9]+}}, r32
  %x1 = mul i64 %a, %b
  %x2 = mul i64 %x1, %c
  %x3 = mul i64 %x2, %d
  %x4 = mul i64 %x3, %a
  %p0 = getelementptr i8, ptr %key, i64 256
  %k0 = load i64, ptr %p0
  %y0 = xor i64 %x4, %k0
  %y1 = mul i64 %y0, %b
  %p1 = getelementptr i8, ptr %key, i64 264
  %k1 = load i64, ptr %p1
  %y2 = xor i64 %y1, %k1
  %y3 = mul i64 %y2, %c
  %p2 = getelementptr i8, ptr %key, i64 272
  %k2 = load i64, ptr %p2
  %y4 = xor i64 %y3, %k2
  %y5 = mul i64 %y4, %d
  %p3 = getelementptr i8, ptr %key, i64 280
  %k3 = load i64, ptr %p3
  %y6 = xor i64 %y5, %k3
  ret i64 %y6
}

; Everything here waits on the loads, so a chain would lengthen the block.
define i64 @critical_words(ptr %p) {
; CHECK-LABEL: critical_words:
; CHECK:       adds r{{[0-9]+}} = 256, r32
; CHECK:       adds r{{[0-9]+}} = 264, r32
; CHECK-NOT:   ], 8
; CHECK:       br.ret
  %p0 = getelementptr i8, ptr %p, i64 256
  %a = load i64, ptr %p0
  %b0 = mul i64 %a, %a
  %b1 = mul i64 %b0, %b0
  %p1 = getelementptr i8, ptr %p, i64 264
  %c = load i64, ptr %p1
  %d0 = mul i64 %c, %c
  %d1 = mul i64 %d0, %d0
  %r = add i64 %b1, %d1
  ret i64 %r
}

declare void @use()

; An address held across a call costs a register the adds do not.
define i64 @across_call(ptr %key, i64 %a) {
; CHECK-LABEL: across_call:
; CHECK:       br.call
; CHECK-NOT:   ], 8
; CHECK:       br.ret
  %p0 = getelementptr i8, ptr %key, i64 256
  %k0 = load i64, ptr %p0
  %x0 = mul i64 %a, %a
  %x1 = xor i64 %x0, %k0
  call void @use()
  %p1 = getelementptr i8, ptr %key, i64 264
  %k1 = load i64, ptr %p1
  %y = xor i64 %x1, %k1
  ret i64 %y
}

; A volatile access keeps its own address.
define i64 @volatile_words(ptr %key, i64 %a, i64 %b) {
; CHECK-LABEL: volatile_words:
; CHECK-NOT:   ], 8
; CHECK:       br.ret
  %x1 = mul i64 %a, %b
  %x2 = mul i64 %x1, %a
  %x3 = mul i64 %x2, %b
  %p0 = getelementptr i8, ptr %key, i64 256
  %k0 = load volatile i64, ptr %p0
  %y0 = xor i64 %x3, %k0
  %y1 = mul i64 %y0, %b
  %p1 = getelementptr i8, ptr %key, i64 264
  %k1 = load volatile i64, ptr %p1
  %y2 = xor i64 %y1, %k1
  ret i64 %y2
}
