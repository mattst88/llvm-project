; RUN: llc -mtriple=ia64-unknown-linux-gnu -mcpu=madison < %s | FileCheck %s --check-prefix=MADISON
; RUN: llc -mtriple=ia64-unknown-linux-gnu < %s | FileCheck %s --check-prefix=DEFAULT

; Two FP loads of one chunk share an L2 bank, and on Madison the second of a
; group takes 6 cycles more, so they get a group each.

define double @fields(ptr %p) {
; MADISON-LABEL: fields:
; MADISON: ldfd {{.*}} ;;{{$}}
; MADISON-NEXT: ldfd
; DEFAULT-LABEL: fields:
; DEFAULT: ldfd {{f[0-9]+ = \[r[0-9]+\](, [0-9]+)?}}{{$}}
; DEFAULT-NEXT: ldfd
  %o = getelementptr inbounds i8, ptr %p, i64 8
  %q = getelementptr inbounds i8, ptr %p, i64 16
  %a = load double, ptr %o
  %b = load double, ptr %q
  %r = fadd double %a, %b
  ret double %r
}

; Loads far apart can share a group.
define double @apart(ptr %p) {
; MADISON-LABEL: apart:
; MADISON: ldfd {{f[0-9]+ = \[r[0-9]+\](, [0-9]+)?}}{{$}}
; MADISON-NEXT: ldfd
  %o = getelementptr inbounds i8, ptr %p, i64 8
  %q = getelementptr inbounds i8, ptr %p, i64 80
  %a = load double, ptr %o
  %b = load double, ptr %q
  %r = fadd double %a, %b
  ret double %r
}
