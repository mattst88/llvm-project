; RUN: not llc -mtriple=ia64-unknown-linux-gnu < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s
; RUN: not llc -mtriple=ia64-unknown-linux-gnu -O0 < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s

; A register stack frame has ninety-six registers, eight of them for the
; outputs, and what a call would replace is kept in the others. A function
; whose assembly names every one of those leaves nowhere to keep it, and is
; refused by name. One that leaves a single local can still save ar.pfs,
; which is all that a function with no call has to save.

; CHECK-NOT: error:
; CHECK: error: {{.*}}in function full void (): a function that leaves no local register for what its prologue saves is not supported on IA-64
; CHECK-NOT: error:

declare void @ext()

define void @full() {
  call void asm sideeffect "nop 0", "~{r32},~{r33},~{r34},~{r35},~{r36},~{r37},~{r38},~{r39},~{r40},~{r41},~{r42},~{r43},~{r44},~{r45},~{r46},~{r47},~{r48},~{r49},~{r50},~{r51},~{r52},~{r53},~{r54},~{r55},~{r56},~{r57},~{r58},~{r59},~{r60},~{r61},~{r62},~{r63},~{r64},~{r65},~{r66},~{r67},~{r68},~{r69},~{r70},~{r71},~{r72},~{r73},~{r74},~{r75},~{r76},~{r77},~{r78},~{r79},~{r80},~{r81},~{r82},~{r83},~{r84},~{r85},~{r86},~{r87},~{r88},~{r89},~{r90},~{r91},~{r92},~{r93},~{r94},~{r95},~{r96},~{r97},~{r98},~{r99},~{r100},~{r101},~{r102},~{r103},~{r104},~{r105},~{r106},~{r107},~{r108},~{r109},~{r110},~{r111},~{r112},~{r113},~{r114},~{r115},~{r116},~{r117},~{r118},~{r119}"()
  call void @ext()
  ret void
}

define void @fits() {
  call void asm sideeffect "nop 0", "~{r33},~{r34},~{r35},~{r36},~{r37},~{r38},~{r39},~{r40},~{r41},~{r42},~{r43},~{r44},~{r45},~{r46},~{r47},~{r48},~{r49},~{r50},~{r51},~{r52},~{r53},~{r54},~{r55},~{r56},~{r57},~{r58},~{r59},~{r60},~{r61},~{r62},~{r63},~{r64},~{r65},~{r66},~{r67},~{r68},~{r69},~{r70},~{r71},~{r72},~{r73},~{r74},~{r75},~{r76},~{r77},~{r78},~{r79},~{r80},~{r81},~{r82},~{r83},~{r84},~{r85},~{r86},~{r87},~{r88},~{r89},~{r90},~{r91},~{r92},~{r93},~{r94},~{r95},~{r96},~{r97},~{r98},~{r99},~{r100},~{r101},~{r102},~{r103},~{r104},~{r105},~{r106},~{r107},~{r108},~{r109},~{r110},~{r111},~{r112},~{r113},~{r114},~{r115},~{r116},~{r117},~{r118},~{r119}"()
  ret void
}
