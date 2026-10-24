; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; Control-speculative loads, which give NaT where a load would fault, and
; the tnat test of a value.

define i64 @ld1(ptr %p) {
; CHECK-LABEL: ld1:
; CHECK:         ld1.s r8 = [r32]
  %v = call i64 @llvm.ia64.ld1.s(ptr %p)
  ret i64 %v
}

define i64 @ld2(ptr %p) {
; CHECK-LABEL: ld2:
; CHECK:         adds r14 = 6, r32 ;;
; CHECK-NEXT:    ld2.s r8 = [r14]
  %q = getelementptr i8, ptr %p, i64 6
  %v = call i64 @llvm.ia64.ld2.s(ptr %q)
  ret i64 %v
}

define i64 @ld4(ptr %p) {
; CHECK-LABEL: ld4:
; CHECK:         ld4.s r8 = [r32]
  %v = call i64 @llvm.ia64.ld4.s(ptr %p)
  ret i64 %v
}

define i64 @ld8(ptr %p) {
; CHECK-LABEL: ld8:
; CHECK:         ld8.s r8 = [r32]
  %v = call i64 @llvm.ia64.ld8.s(ptr %p)
  ret i64 %v
}

define i64 @isnat(i64 %x) {
; CHECK-LABEL: isnat:
; CHECK:         tnat.z p7, p6 = r32
; CHECK-NEXT:    mov r8 = r0 ;;
; CHECK-NEXT:    (p6) mov r8 = 1
  %v = call i64 @llvm.ia64.tnat(i64 %x)
  ret i64 %v
}

; If the speculative load gave NaT, load again.
define i64 @recover(ptr %p) {
; CHECK-LABEL: recover:
; CHECK:         ld8.s r8 = [r32] ;;
; CHECK-NEXT:    tnat.z p6, p7 = r8 ;;
; CHECK-NEXT:    (p7) ld8 r8 = [r32]
entry:
  %v = call i64 @llvm.ia64.ld8.s(ptr %p)
  %t = call i64 @llvm.ia64.tnat(i64 %v)
  %c = icmp ne i64 %t, 0
  br i1 %c, label %nat, label %ok
nat:
  %w = load i64, ptr %p
  ret i64 %w
ok:
  ret i64 %v
}

; A NaT spilled with st8 would fault, so where values may be NaT the spills
; are st8.spill and the reloads ld8.fill, which keep it in ar.unat.
define void @spill(ptr %p, ptr %q) {
; CHECK-LABEL: spill:
; CHECK:         mov r116 = ar.unat
; CHECK:         .body
; CHECK:         ld8.s [[V:r[0-9]+]] = [r32]
; CHECK:         st8.spill [r2] = [[V]]
; CHECK-NOT:     {{^}}	st8 [r2]
; CHECK:         ld8.fill
; CHECK:         mov ar.unat = r116
  %a0 = getelementptr i64, ptr %p, i64 0
  %v0 = call i64 @llvm.ia64.ld8.s(ptr %a0)
  %a1 = getelementptr i64, ptr %p, i64 1
  %v1 = call i64 @llvm.ia64.ld8.s(ptr %a1)
  %a2 = getelementptr i64, ptr %p, i64 2
  %v2 = call i64 @llvm.ia64.ld8.s(ptr %a2)
  %a3 = getelementptr i64, ptr %p, i64 3
  %v3 = call i64 @llvm.ia64.ld8.s(ptr %a3)
  %a4 = getelementptr i64, ptr %p, i64 4
  %v4 = call i64 @llvm.ia64.ld8.s(ptr %a4)
  %a5 = getelementptr i64, ptr %p, i64 5
  %v5 = call i64 @llvm.ia64.ld8.s(ptr %a5)
  %a6 = getelementptr i64, ptr %p, i64 6
  %v6 = call i64 @llvm.ia64.ld8.s(ptr %a6)
  %a7 = getelementptr i64, ptr %p, i64 7
  %v7 = call i64 @llvm.ia64.ld8.s(ptr %a7)
  %a8 = getelementptr i64, ptr %p, i64 8
  %v8 = call i64 @llvm.ia64.ld8.s(ptr %a8)
  %a9 = getelementptr i64, ptr %p, i64 9
  %v9 = call i64 @llvm.ia64.ld8.s(ptr %a9)
  %a10 = getelementptr i64, ptr %p, i64 10
  %v10 = call i64 @llvm.ia64.ld8.s(ptr %a10)
  %a11 = getelementptr i64, ptr %p, i64 11
  %v11 = call i64 @llvm.ia64.ld8.s(ptr %a11)
  %a12 = getelementptr i64, ptr %p, i64 12
  %v12 = call i64 @llvm.ia64.ld8.s(ptr %a12)
  %a13 = getelementptr i64, ptr %p, i64 13
  %v13 = call i64 @llvm.ia64.ld8.s(ptr %a13)
  %a14 = getelementptr i64, ptr %p, i64 14
  %v14 = call i64 @llvm.ia64.ld8.s(ptr %a14)
  %a15 = getelementptr i64, ptr %p, i64 15
  %v15 = call i64 @llvm.ia64.ld8.s(ptr %a15)
  %a16 = getelementptr i64, ptr %p, i64 16
  %v16 = call i64 @llvm.ia64.ld8.s(ptr %a16)
  %a17 = getelementptr i64, ptr %p, i64 17
  %v17 = call i64 @llvm.ia64.ld8.s(ptr %a17)
  %a18 = getelementptr i64, ptr %p, i64 18
  %v18 = call i64 @llvm.ia64.ld8.s(ptr %a18)
  %a19 = getelementptr i64, ptr %p, i64 19
  %v19 = call i64 @llvm.ia64.ld8.s(ptr %a19)
  %a20 = getelementptr i64, ptr %p, i64 20
  %v20 = call i64 @llvm.ia64.ld8.s(ptr %a20)
  %a21 = getelementptr i64, ptr %p, i64 21
  %v21 = call i64 @llvm.ia64.ld8.s(ptr %a21)
  %a22 = getelementptr i64, ptr %p, i64 22
  %v22 = call i64 @llvm.ia64.ld8.s(ptr %a22)
  %a23 = getelementptr i64, ptr %p, i64 23
  %v23 = call i64 @llvm.ia64.ld8.s(ptr %a23)
  %a24 = getelementptr i64, ptr %p, i64 24
  %v24 = call i64 @llvm.ia64.ld8.s(ptr %a24)
  %a25 = getelementptr i64, ptr %p, i64 25
  %v25 = call i64 @llvm.ia64.ld8.s(ptr %a25)
  %a26 = getelementptr i64, ptr %p, i64 26
  %v26 = call i64 @llvm.ia64.ld8.s(ptr %a26)
  %a27 = getelementptr i64, ptr %p, i64 27
  %v27 = call i64 @llvm.ia64.ld8.s(ptr %a27)
  %a28 = getelementptr i64, ptr %p, i64 28
  %v28 = call i64 @llvm.ia64.ld8.s(ptr %a28)
  %a29 = getelementptr i64, ptr %p, i64 29
  %v29 = call i64 @llvm.ia64.ld8.s(ptr %a29)
  %a30 = getelementptr i64, ptr %p, i64 30
  %v30 = call i64 @llvm.ia64.ld8.s(ptr %a30)
  %a31 = getelementptr i64, ptr %p, i64 31
  %v31 = call i64 @llvm.ia64.ld8.s(ptr %a31)
  %a32 = getelementptr i64, ptr %p, i64 32
  %v32 = call i64 @llvm.ia64.ld8.s(ptr %a32)
  %a33 = getelementptr i64, ptr %p, i64 33
  %v33 = call i64 @llvm.ia64.ld8.s(ptr %a33)
  %a34 = getelementptr i64, ptr %p, i64 34
  %v34 = call i64 @llvm.ia64.ld8.s(ptr %a34)
  %a35 = getelementptr i64, ptr %p, i64 35
  %v35 = call i64 @llvm.ia64.ld8.s(ptr %a35)
  %a36 = getelementptr i64, ptr %p, i64 36
  %v36 = call i64 @llvm.ia64.ld8.s(ptr %a36)
  %a37 = getelementptr i64, ptr %p, i64 37
  %v37 = call i64 @llvm.ia64.ld8.s(ptr %a37)
  %a38 = getelementptr i64, ptr %p, i64 38
  %v38 = call i64 @llvm.ia64.ld8.s(ptr %a38)
  %a39 = getelementptr i64, ptr %p, i64 39
  %v39 = call i64 @llvm.ia64.ld8.s(ptr %a39)
  %a40 = getelementptr i64, ptr %p, i64 40
  %v40 = call i64 @llvm.ia64.ld8.s(ptr %a40)
  %a41 = getelementptr i64, ptr %p, i64 41
  %v41 = call i64 @llvm.ia64.ld8.s(ptr %a41)
  %a42 = getelementptr i64, ptr %p, i64 42
  %v42 = call i64 @llvm.ia64.ld8.s(ptr %a42)
  %a43 = getelementptr i64, ptr %p, i64 43
  %v43 = call i64 @llvm.ia64.ld8.s(ptr %a43)
  %a44 = getelementptr i64, ptr %p, i64 44
  %v44 = call i64 @llvm.ia64.ld8.s(ptr %a44)
  %a45 = getelementptr i64, ptr %p, i64 45
  %v45 = call i64 @llvm.ia64.ld8.s(ptr %a45)
  %a46 = getelementptr i64, ptr %p, i64 46
  %v46 = call i64 @llvm.ia64.ld8.s(ptr %a46)
  %a47 = getelementptr i64, ptr %p, i64 47
  %v47 = call i64 @llvm.ia64.ld8.s(ptr %a47)
  %a48 = getelementptr i64, ptr %p, i64 48
  %v48 = call i64 @llvm.ia64.ld8.s(ptr %a48)
  %a49 = getelementptr i64, ptr %p, i64 49
  %v49 = call i64 @llvm.ia64.ld8.s(ptr %a49)
  %a50 = getelementptr i64, ptr %p, i64 50
  %v50 = call i64 @llvm.ia64.ld8.s(ptr %a50)
  %a51 = getelementptr i64, ptr %p, i64 51
  %v51 = call i64 @llvm.ia64.ld8.s(ptr %a51)
  %a52 = getelementptr i64, ptr %p, i64 52
  %v52 = call i64 @llvm.ia64.ld8.s(ptr %a52)
  %a53 = getelementptr i64, ptr %p, i64 53
  %v53 = call i64 @llvm.ia64.ld8.s(ptr %a53)
  %a54 = getelementptr i64, ptr %p, i64 54
  %v54 = call i64 @llvm.ia64.ld8.s(ptr %a54)
  %a55 = getelementptr i64, ptr %p, i64 55
  %v55 = call i64 @llvm.ia64.ld8.s(ptr %a55)
  %a56 = getelementptr i64, ptr %p, i64 56
  %v56 = call i64 @llvm.ia64.ld8.s(ptr %a56)
  %a57 = getelementptr i64, ptr %p, i64 57
  %v57 = call i64 @llvm.ia64.ld8.s(ptr %a57)
  %a58 = getelementptr i64, ptr %p, i64 58
  %v58 = call i64 @llvm.ia64.ld8.s(ptr %a58)
  %a59 = getelementptr i64, ptr %p, i64 59
  %v59 = call i64 @llvm.ia64.ld8.s(ptr %a59)
  %a60 = getelementptr i64, ptr %p, i64 60
  %v60 = call i64 @llvm.ia64.ld8.s(ptr %a60)
  %a61 = getelementptr i64, ptr %p, i64 61
  %v61 = call i64 @llvm.ia64.ld8.s(ptr %a61)
  %a62 = getelementptr i64, ptr %p, i64 62
  %v62 = call i64 @llvm.ia64.ld8.s(ptr %a62)
  %a63 = getelementptr i64, ptr %p, i64 63
  %v63 = call i64 @llvm.ia64.ld8.s(ptr %a63)
  %a64 = getelementptr i64, ptr %p, i64 64
  %v64 = call i64 @llvm.ia64.ld8.s(ptr %a64)
  %a65 = getelementptr i64, ptr %p, i64 65
  %v65 = call i64 @llvm.ia64.ld8.s(ptr %a65)
  %a66 = getelementptr i64, ptr %p, i64 66
  %v66 = call i64 @llvm.ia64.ld8.s(ptr %a66)
  %a67 = getelementptr i64, ptr %p, i64 67
  %v67 = call i64 @llvm.ia64.ld8.s(ptr %a67)
  %a68 = getelementptr i64, ptr %p, i64 68
  %v68 = call i64 @llvm.ia64.ld8.s(ptr %a68)
  %a69 = getelementptr i64, ptr %p, i64 69
  %v69 = call i64 @llvm.ia64.ld8.s(ptr %a69)
  %a70 = getelementptr i64, ptr %p, i64 70
  %v70 = call i64 @llvm.ia64.ld8.s(ptr %a70)
  %a71 = getelementptr i64, ptr %p, i64 71
  %v71 = call i64 @llvm.ia64.ld8.s(ptr %a71)
  %a72 = getelementptr i64, ptr %p, i64 72
  %v72 = call i64 @llvm.ia64.ld8.s(ptr %a72)
  %a73 = getelementptr i64, ptr %p, i64 73
  %v73 = call i64 @llvm.ia64.ld8.s(ptr %a73)
  %a74 = getelementptr i64, ptr %p, i64 74
  %v74 = call i64 @llvm.ia64.ld8.s(ptr %a74)
  %a75 = getelementptr i64, ptr %p, i64 75
  %v75 = call i64 @llvm.ia64.ld8.s(ptr %a75)
  %a76 = getelementptr i64, ptr %p, i64 76
  %v76 = call i64 @llvm.ia64.ld8.s(ptr %a76)
  %a77 = getelementptr i64, ptr %p, i64 77
  %v77 = call i64 @llvm.ia64.ld8.s(ptr %a77)
  %a78 = getelementptr i64, ptr %p, i64 78
  %v78 = call i64 @llvm.ia64.ld8.s(ptr %a78)
  %a79 = getelementptr i64, ptr %p, i64 79
  %v79 = call i64 @llvm.ia64.ld8.s(ptr %a79)
  %a80 = getelementptr i64, ptr %p, i64 80
  %v80 = call i64 @llvm.ia64.ld8.s(ptr %a80)
  %a81 = getelementptr i64, ptr %p, i64 81
  %v81 = call i64 @llvm.ia64.ld8.s(ptr %a81)
  %a82 = getelementptr i64, ptr %p, i64 82
  %v82 = call i64 @llvm.ia64.ld8.s(ptr %a82)
  %a83 = getelementptr i64, ptr %p, i64 83
  %v83 = call i64 @llvm.ia64.ld8.s(ptr %a83)
  %a84 = getelementptr i64, ptr %p, i64 84
  %v84 = call i64 @llvm.ia64.ld8.s(ptr %a84)
  %a85 = getelementptr i64, ptr %p, i64 85
  %v85 = call i64 @llvm.ia64.ld8.s(ptr %a85)
  %a86 = getelementptr i64, ptr %p, i64 86
  %v86 = call i64 @llvm.ia64.ld8.s(ptr %a86)
  %a87 = getelementptr i64, ptr %p, i64 87
  %v87 = call i64 @llvm.ia64.ld8.s(ptr %a87)
  %a88 = getelementptr i64, ptr %p, i64 88
  %v88 = call i64 @llvm.ia64.ld8.s(ptr %a88)
  %a89 = getelementptr i64, ptr %p, i64 89
  %v89 = call i64 @llvm.ia64.ld8.s(ptr %a89)
  %a90 = getelementptr i64, ptr %p, i64 90
  %v90 = call i64 @llvm.ia64.ld8.s(ptr %a90)
  %a91 = getelementptr i64, ptr %p, i64 91
  %v91 = call i64 @llvm.ia64.ld8.s(ptr %a91)
  %a92 = getelementptr i64, ptr %p, i64 92
  %v92 = call i64 @llvm.ia64.ld8.s(ptr %a92)
  %a93 = getelementptr i64, ptr %p, i64 93
  %v93 = call i64 @llvm.ia64.ld8.s(ptr %a93)
  %a94 = getelementptr i64, ptr %p, i64 94
  %v94 = call i64 @llvm.ia64.ld8.s(ptr %a94)
  %a95 = getelementptr i64, ptr %p, i64 95
  %v95 = call i64 @llvm.ia64.ld8.s(ptr %a95)
  %a96 = getelementptr i64, ptr %p, i64 96
  %v96 = call i64 @llvm.ia64.ld8.s(ptr %a96)
  %a97 = getelementptr i64, ptr %p, i64 97
  %v97 = call i64 @llvm.ia64.ld8.s(ptr %a97)
  %a98 = getelementptr i64, ptr %p, i64 98
  %v98 = call i64 @llvm.ia64.ld8.s(ptr %a98)
  %a99 = getelementptr i64, ptr %p, i64 99
  %v99 = call i64 @llvm.ia64.ld8.s(ptr %a99)
  %a100 = getelementptr i64, ptr %p, i64 100
  %v100 = call i64 @llvm.ia64.ld8.s(ptr %a100)
  %a101 = getelementptr i64, ptr %p, i64 101
  %v101 = call i64 @llvm.ia64.ld8.s(ptr %a101)
  %a102 = getelementptr i64, ptr %p, i64 102
  %v102 = call i64 @llvm.ia64.ld8.s(ptr %a102)
  %a103 = getelementptr i64, ptr %p, i64 103
  %v103 = call i64 @llvm.ia64.ld8.s(ptr %a103)
  %a104 = getelementptr i64, ptr %p, i64 104
  %v104 = call i64 @llvm.ia64.ld8.s(ptr %a104)
  %a105 = getelementptr i64, ptr %p, i64 105
  %v105 = call i64 @llvm.ia64.ld8.s(ptr %a105)
  %a106 = getelementptr i64, ptr %p, i64 106
  %v106 = call i64 @llvm.ia64.ld8.s(ptr %a106)
  %a107 = getelementptr i64, ptr %p, i64 107
  %v107 = call i64 @llvm.ia64.ld8.s(ptr %a107)
  %a108 = getelementptr i64, ptr %p, i64 108
  %v108 = call i64 @llvm.ia64.ld8.s(ptr %a108)
  %a109 = getelementptr i64, ptr %p, i64 109
  %v109 = call i64 @llvm.ia64.ld8.s(ptr %a109)
  %a110 = getelementptr i64, ptr %p, i64 110
  %v110 = call i64 @llvm.ia64.ld8.s(ptr %a110)
  %a111 = getelementptr i64, ptr %p, i64 111
  %v111 = call i64 @llvm.ia64.ld8.s(ptr %a111)
  %a112 = getelementptr i64, ptr %p, i64 112
  %v112 = call i64 @llvm.ia64.ld8.s(ptr %a112)
  %a113 = getelementptr i64, ptr %p, i64 113
  %v113 = call i64 @llvm.ia64.ld8.s(ptr %a113)
  %a114 = getelementptr i64, ptr %p, i64 114
  %v114 = call i64 @llvm.ia64.ld8.s(ptr %a114)
  %a115 = getelementptr i64, ptr %p, i64 115
  %v115 = call i64 @llvm.ia64.ld8.s(ptr %a115)
  %a116 = getelementptr i64, ptr %p, i64 116
  %v116 = call i64 @llvm.ia64.ld8.s(ptr %a116)
  %a117 = getelementptr i64, ptr %p, i64 117
  %v117 = call i64 @llvm.ia64.ld8.s(ptr %a117)
  %a118 = getelementptr i64, ptr %p, i64 118
  %v118 = call i64 @llvm.ia64.ld8.s(ptr %a118)
  %a119 = getelementptr i64, ptr %p, i64 119
  %v119 = call i64 @llvm.ia64.ld8.s(ptr %a119)
  store volatile i64 %v119, ptr %q
  store volatile i64 %v118, ptr %q
  store volatile i64 %v117, ptr %q
  store volatile i64 %v116, ptr %q
  store volatile i64 %v115, ptr %q
  store volatile i64 %v114, ptr %q
  store volatile i64 %v113, ptr %q
  store volatile i64 %v112, ptr %q
  store volatile i64 %v111, ptr %q
  store volatile i64 %v110, ptr %q
  store volatile i64 %v109, ptr %q
  store volatile i64 %v108, ptr %q
  store volatile i64 %v107, ptr %q
  store volatile i64 %v106, ptr %q
  store volatile i64 %v105, ptr %q
  store volatile i64 %v104, ptr %q
  store volatile i64 %v103, ptr %q
  store volatile i64 %v102, ptr %q
  store volatile i64 %v101, ptr %q
  store volatile i64 %v100, ptr %q
  store volatile i64 %v99, ptr %q
  store volatile i64 %v98, ptr %q
  store volatile i64 %v97, ptr %q
  store volatile i64 %v96, ptr %q
  store volatile i64 %v95, ptr %q
  store volatile i64 %v94, ptr %q
  store volatile i64 %v93, ptr %q
  store volatile i64 %v92, ptr %q
  store volatile i64 %v91, ptr %q
  store volatile i64 %v90, ptr %q
  store volatile i64 %v89, ptr %q
  store volatile i64 %v88, ptr %q
  store volatile i64 %v87, ptr %q
  store volatile i64 %v86, ptr %q
  store volatile i64 %v85, ptr %q
  store volatile i64 %v84, ptr %q
  store volatile i64 %v83, ptr %q
  store volatile i64 %v82, ptr %q
  store volatile i64 %v81, ptr %q
  store volatile i64 %v80, ptr %q
  store volatile i64 %v79, ptr %q
  store volatile i64 %v78, ptr %q
  store volatile i64 %v77, ptr %q
  store volatile i64 %v76, ptr %q
  store volatile i64 %v75, ptr %q
  store volatile i64 %v74, ptr %q
  store volatile i64 %v73, ptr %q
  store volatile i64 %v72, ptr %q
  store volatile i64 %v71, ptr %q
  store volatile i64 %v70, ptr %q
  store volatile i64 %v69, ptr %q
  store volatile i64 %v68, ptr %q
  store volatile i64 %v67, ptr %q
  store volatile i64 %v66, ptr %q
  store volatile i64 %v65, ptr %q
  store volatile i64 %v64, ptr %q
  store volatile i64 %v63, ptr %q
  store volatile i64 %v62, ptr %q
  store volatile i64 %v61, ptr %q
  store volatile i64 %v60, ptr %q
  store volatile i64 %v59, ptr %q
  store volatile i64 %v58, ptr %q
  store volatile i64 %v57, ptr %q
  store volatile i64 %v56, ptr %q
  store volatile i64 %v55, ptr %q
  store volatile i64 %v54, ptr %q
  store volatile i64 %v53, ptr %q
  store volatile i64 %v52, ptr %q
  store volatile i64 %v51, ptr %q
  store volatile i64 %v50, ptr %q
  store volatile i64 %v49, ptr %q
  store volatile i64 %v48, ptr %q
  store volatile i64 %v47, ptr %q
  store volatile i64 %v46, ptr %q
  store volatile i64 %v45, ptr %q
  store volatile i64 %v44, ptr %q
  store volatile i64 %v43, ptr %q
  store volatile i64 %v42, ptr %q
  store volatile i64 %v41, ptr %q
  store volatile i64 %v40, ptr %q
  store volatile i64 %v39, ptr %q
  store volatile i64 %v38, ptr %q
  store volatile i64 %v37, ptr %q
  store volatile i64 %v36, ptr %q
  store volatile i64 %v35, ptr %q
  store volatile i64 %v34, ptr %q
  store volatile i64 %v33, ptr %q
  store volatile i64 %v32, ptr %q
  store volatile i64 %v31, ptr %q
  store volatile i64 %v30, ptr %q
  store volatile i64 %v29, ptr %q
  store volatile i64 %v28, ptr %q
  store volatile i64 %v27, ptr %q
  store volatile i64 %v26, ptr %q
  store volatile i64 %v25, ptr %q
  store volatile i64 %v24, ptr %q
  store volatile i64 %v23, ptr %q
  store volatile i64 %v22, ptr %q
  store volatile i64 %v21, ptr %q
  store volatile i64 %v20, ptr %q
  store volatile i64 %v19, ptr %q
  store volatile i64 %v18, ptr %q
  store volatile i64 %v17, ptr %q
  store volatile i64 %v16, ptr %q
  store volatile i64 %v15, ptr %q
  store volatile i64 %v14, ptr %q
  store volatile i64 %v13, ptr %q
  store volatile i64 %v12, ptr %q
  store volatile i64 %v11, ptr %q
  store volatile i64 %v10, ptr %q
  store volatile i64 %v9, ptr %q
  store volatile i64 %v8, ptr %q
  store volatile i64 %v7, ptr %q
  store volatile i64 %v6, ptr %q
  store volatile i64 %v5, ptr %q
  store volatile i64 %v4, ptr %q
  store volatile i64 %v3, ptr %q
  store volatile i64 %v2, ptr %q
  store volatile i64 %v1, ptr %q
  store volatile i64 %v0, ptr %q
  ret void
}

; Where the frame is too large for ar.unat to tell spill slots apart, the
; loads give 0 and every test says NaT.
define i64 @large_frame(ptr %p, ptr %q) {
; CHECK-LABEL: large_frame:
; CHECK-NOT:     ld8.s
; CHECK:         br.call.sptk.many b0 = use
; CHECK:         mov [[V:r[0-9]+]] = 0
; CHECK-NOT:     ld8.s
; CHECK-NOT:     tnat
; CHECK:         cmp.eq p6, p7 = 0, r0
; CHECK:         (p6) mov r8 = 1
; CHECK-NOT:     ld8.s
; CHECK-LABEL:   .Lfunc_end
  %buf = alloca [600 x i8]
  call void @use(ptr %buf)
  %a0 = getelementptr i64, ptr %p, i64 0
  %v0 = call i64 @llvm.ia64.ld8.s(ptr %a0)
  %a1 = getelementptr i64, ptr %p, i64 1
  %v1 = call i64 @llvm.ia64.ld8.s(ptr %a1)
  %a2 = getelementptr i64, ptr %p, i64 2
  %v2 = call i64 @llvm.ia64.ld8.s(ptr %a2)
  %a3 = getelementptr i64, ptr %p, i64 3
  %v3 = call i64 @llvm.ia64.ld8.s(ptr %a3)
  %a4 = getelementptr i64, ptr %p, i64 4
  %v4 = call i64 @llvm.ia64.ld8.s(ptr %a4)
  %a5 = getelementptr i64, ptr %p, i64 5
  %v5 = call i64 @llvm.ia64.ld8.s(ptr %a5)
  %a6 = getelementptr i64, ptr %p, i64 6
  %v6 = call i64 @llvm.ia64.ld8.s(ptr %a6)
  %a7 = getelementptr i64, ptr %p, i64 7
  %v7 = call i64 @llvm.ia64.ld8.s(ptr %a7)
  %a8 = getelementptr i64, ptr %p, i64 8
  %v8 = call i64 @llvm.ia64.ld8.s(ptr %a8)
  %a9 = getelementptr i64, ptr %p, i64 9
  %v9 = call i64 @llvm.ia64.ld8.s(ptr %a9)
  %a10 = getelementptr i64, ptr %p, i64 10
  %v10 = call i64 @llvm.ia64.ld8.s(ptr %a10)
  %a11 = getelementptr i64, ptr %p, i64 11
  %v11 = call i64 @llvm.ia64.ld8.s(ptr %a11)
  %a12 = getelementptr i64, ptr %p, i64 12
  %v12 = call i64 @llvm.ia64.ld8.s(ptr %a12)
  %a13 = getelementptr i64, ptr %p, i64 13
  %v13 = call i64 @llvm.ia64.ld8.s(ptr %a13)
  %a14 = getelementptr i64, ptr %p, i64 14
  %v14 = call i64 @llvm.ia64.ld8.s(ptr %a14)
  %a15 = getelementptr i64, ptr %p, i64 15
  %v15 = call i64 @llvm.ia64.ld8.s(ptr %a15)
  %a16 = getelementptr i64, ptr %p, i64 16
  %v16 = call i64 @llvm.ia64.ld8.s(ptr %a16)
  %a17 = getelementptr i64, ptr %p, i64 17
  %v17 = call i64 @llvm.ia64.ld8.s(ptr %a17)
  %a18 = getelementptr i64, ptr %p, i64 18
  %v18 = call i64 @llvm.ia64.ld8.s(ptr %a18)
  %a19 = getelementptr i64, ptr %p, i64 19
  %v19 = call i64 @llvm.ia64.ld8.s(ptr %a19)
  %a20 = getelementptr i64, ptr %p, i64 20
  %v20 = call i64 @llvm.ia64.ld8.s(ptr %a20)
  %a21 = getelementptr i64, ptr %p, i64 21
  %v21 = call i64 @llvm.ia64.ld8.s(ptr %a21)
  %a22 = getelementptr i64, ptr %p, i64 22
  %v22 = call i64 @llvm.ia64.ld8.s(ptr %a22)
  %a23 = getelementptr i64, ptr %p, i64 23
  %v23 = call i64 @llvm.ia64.ld8.s(ptr %a23)
  %a24 = getelementptr i64, ptr %p, i64 24
  %v24 = call i64 @llvm.ia64.ld8.s(ptr %a24)
  %a25 = getelementptr i64, ptr %p, i64 25
  %v25 = call i64 @llvm.ia64.ld8.s(ptr %a25)
  %a26 = getelementptr i64, ptr %p, i64 26
  %v26 = call i64 @llvm.ia64.ld8.s(ptr %a26)
  %a27 = getelementptr i64, ptr %p, i64 27
  %v27 = call i64 @llvm.ia64.ld8.s(ptr %a27)
  %a28 = getelementptr i64, ptr %p, i64 28
  %v28 = call i64 @llvm.ia64.ld8.s(ptr %a28)
  %a29 = getelementptr i64, ptr %p, i64 29
  %v29 = call i64 @llvm.ia64.ld8.s(ptr %a29)
  %a30 = getelementptr i64, ptr %p, i64 30
  %v30 = call i64 @llvm.ia64.ld8.s(ptr %a30)
  %a31 = getelementptr i64, ptr %p, i64 31
  %v31 = call i64 @llvm.ia64.ld8.s(ptr %a31)
  %a32 = getelementptr i64, ptr %p, i64 32
  %v32 = call i64 @llvm.ia64.ld8.s(ptr %a32)
  %a33 = getelementptr i64, ptr %p, i64 33
  %v33 = call i64 @llvm.ia64.ld8.s(ptr %a33)
  %a34 = getelementptr i64, ptr %p, i64 34
  %v34 = call i64 @llvm.ia64.ld8.s(ptr %a34)
  %a35 = getelementptr i64, ptr %p, i64 35
  %v35 = call i64 @llvm.ia64.ld8.s(ptr %a35)
  %a36 = getelementptr i64, ptr %p, i64 36
  %v36 = call i64 @llvm.ia64.ld8.s(ptr %a36)
  %a37 = getelementptr i64, ptr %p, i64 37
  %v37 = call i64 @llvm.ia64.ld8.s(ptr %a37)
  %a38 = getelementptr i64, ptr %p, i64 38
  %v38 = call i64 @llvm.ia64.ld8.s(ptr %a38)
  %a39 = getelementptr i64, ptr %p, i64 39
  %v39 = call i64 @llvm.ia64.ld8.s(ptr %a39)
  %a40 = getelementptr i64, ptr %p, i64 40
  %v40 = call i64 @llvm.ia64.ld8.s(ptr %a40)
  %a41 = getelementptr i64, ptr %p, i64 41
  %v41 = call i64 @llvm.ia64.ld8.s(ptr %a41)
  %a42 = getelementptr i64, ptr %p, i64 42
  %v42 = call i64 @llvm.ia64.ld8.s(ptr %a42)
  %a43 = getelementptr i64, ptr %p, i64 43
  %v43 = call i64 @llvm.ia64.ld8.s(ptr %a43)
  %a44 = getelementptr i64, ptr %p, i64 44
  %v44 = call i64 @llvm.ia64.ld8.s(ptr %a44)
  %a45 = getelementptr i64, ptr %p, i64 45
  %v45 = call i64 @llvm.ia64.ld8.s(ptr %a45)
  %a46 = getelementptr i64, ptr %p, i64 46
  %v46 = call i64 @llvm.ia64.ld8.s(ptr %a46)
  %a47 = getelementptr i64, ptr %p, i64 47
  %v47 = call i64 @llvm.ia64.ld8.s(ptr %a47)
  %a48 = getelementptr i64, ptr %p, i64 48
  %v48 = call i64 @llvm.ia64.ld8.s(ptr %a48)
  %a49 = getelementptr i64, ptr %p, i64 49
  %v49 = call i64 @llvm.ia64.ld8.s(ptr %a49)
  %a50 = getelementptr i64, ptr %p, i64 50
  %v50 = call i64 @llvm.ia64.ld8.s(ptr %a50)
  %a51 = getelementptr i64, ptr %p, i64 51
  %v51 = call i64 @llvm.ia64.ld8.s(ptr %a51)
  %a52 = getelementptr i64, ptr %p, i64 52
  %v52 = call i64 @llvm.ia64.ld8.s(ptr %a52)
  %a53 = getelementptr i64, ptr %p, i64 53
  %v53 = call i64 @llvm.ia64.ld8.s(ptr %a53)
  %a54 = getelementptr i64, ptr %p, i64 54
  %v54 = call i64 @llvm.ia64.ld8.s(ptr %a54)
  %a55 = getelementptr i64, ptr %p, i64 55
  %v55 = call i64 @llvm.ia64.ld8.s(ptr %a55)
  %a56 = getelementptr i64, ptr %p, i64 56
  %v56 = call i64 @llvm.ia64.ld8.s(ptr %a56)
  %a57 = getelementptr i64, ptr %p, i64 57
  %v57 = call i64 @llvm.ia64.ld8.s(ptr %a57)
  %a58 = getelementptr i64, ptr %p, i64 58
  %v58 = call i64 @llvm.ia64.ld8.s(ptr %a58)
  %a59 = getelementptr i64, ptr %p, i64 59
  %v59 = call i64 @llvm.ia64.ld8.s(ptr %a59)
  %a60 = getelementptr i64, ptr %p, i64 60
  %v60 = call i64 @llvm.ia64.ld8.s(ptr %a60)
  %a61 = getelementptr i64, ptr %p, i64 61
  %v61 = call i64 @llvm.ia64.ld8.s(ptr %a61)
  %a62 = getelementptr i64, ptr %p, i64 62
  %v62 = call i64 @llvm.ia64.ld8.s(ptr %a62)
  %a63 = getelementptr i64, ptr %p, i64 63
  %v63 = call i64 @llvm.ia64.ld8.s(ptr %a63)
  %a64 = getelementptr i64, ptr %p, i64 64
  %v64 = call i64 @llvm.ia64.ld8.s(ptr %a64)
  %a65 = getelementptr i64, ptr %p, i64 65
  %v65 = call i64 @llvm.ia64.ld8.s(ptr %a65)
  %a66 = getelementptr i64, ptr %p, i64 66
  %v66 = call i64 @llvm.ia64.ld8.s(ptr %a66)
  %a67 = getelementptr i64, ptr %p, i64 67
  %v67 = call i64 @llvm.ia64.ld8.s(ptr %a67)
  %a68 = getelementptr i64, ptr %p, i64 68
  %v68 = call i64 @llvm.ia64.ld8.s(ptr %a68)
  %a69 = getelementptr i64, ptr %p, i64 69
  %v69 = call i64 @llvm.ia64.ld8.s(ptr %a69)
  %a70 = getelementptr i64, ptr %p, i64 70
  %v70 = call i64 @llvm.ia64.ld8.s(ptr %a70)
  %a71 = getelementptr i64, ptr %p, i64 71
  %v71 = call i64 @llvm.ia64.ld8.s(ptr %a71)
  %a72 = getelementptr i64, ptr %p, i64 72
  %v72 = call i64 @llvm.ia64.ld8.s(ptr %a72)
  %a73 = getelementptr i64, ptr %p, i64 73
  %v73 = call i64 @llvm.ia64.ld8.s(ptr %a73)
  %a74 = getelementptr i64, ptr %p, i64 74
  %v74 = call i64 @llvm.ia64.ld8.s(ptr %a74)
  %a75 = getelementptr i64, ptr %p, i64 75
  %v75 = call i64 @llvm.ia64.ld8.s(ptr %a75)
  %a76 = getelementptr i64, ptr %p, i64 76
  %v76 = call i64 @llvm.ia64.ld8.s(ptr %a76)
  %a77 = getelementptr i64, ptr %p, i64 77
  %v77 = call i64 @llvm.ia64.ld8.s(ptr %a77)
  %a78 = getelementptr i64, ptr %p, i64 78
  %v78 = call i64 @llvm.ia64.ld8.s(ptr %a78)
  %a79 = getelementptr i64, ptr %p, i64 79
  %v79 = call i64 @llvm.ia64.ld8.s(ptr %a79)
  %a80 = getelementptr i64, ptr %p, i64 80
  %v80 = call i64 @llvm.ia64.ld8.s(ptr %a80)
  %a81 = getelementptr i64, ptr %p, i64 81
  %v81 = call i64 @llvm.ia64.ld8.s(ptr %a81)
  %a82 = getelementptr i64, ptr %p, i64 82
  %v82 = call i64 @llvm.ia64.ld8.s(ptr %a82)
  %a83 = getelementptr i64, ptr %p, i64 83
  %v83 = call i64 @llvm.ia64.ld8.s(ptr %a83)
  %a84 = getelementptr i64, ptr %p, i64 84
  %v84 = call i64 @llvm.ia64.ld8.s(ptr %a84)
  %a85 = getelementptr i64, ptr %p, i64 85
  %v85 = call i64 @llvm.ia64.ld8.s(ptr %a85)
  %a86 = getelementptr i64, ptr %p, i64 86
  %v86 = call i64 @llvm.ia64.ld8.s(ptr %a86)
  %a87 = getelementptr i64, ptr %p, i64 87
  %v87 = call i64 @llvm.ia64.ld8.s(ptr %a87)
  %a88 = getelementptr i64, ptr %p, i64 88
  %v88 = call i64 @llvm.ia64.ld8.s(ptr %a88)
  %a89 = getelementptr i64, ptr %p, i64 89
  %v89 = call i64 @llvm.ia64.ld8.s(ptr %a89)
  %a90 = getelementptr i64, ptr %p, i64 90
  %v90 = call i64 @llvm.ia64.ld8.s(ptr %a90)
  %a91 = getelementptr i64, ptr %p, i64 91
  %v91 = call i64 @llvm.ia64.ld8.s(ptr %a91)
  %a92 = getelementptr i64, ptr %p, i64 92
  %v92 = call i64 @llvm.ia64.ld8.s(ptr %a92)
  %a93 = getelementptr i64, ptr %p, i64 93
  %v93 = call i64 @llvm.ia64.ld8.s(ptr %a93)
  %a94 = getelementptr i64, ptr %p, i64 94
  %v94 = call i64 @llvm.ia64.ld8.s(ptr %a94)
  %a95 = getelementptr i64, ptr %p, i64 95
  %v95 = call i64 @llvm.ia64.ld8.s(ptr %a95)
  %a96 = getelementptr i64, ptr %p, i64 96
  %v96 = call i64 @llvm.ia64.ld8.s(ptr %a96)
  %a97 = getelementptr i64, ptr %p, i64 97
  %v97 = call i64 @llvm.ia64.ld8.s(ptr %a97)
  %a98 = getelementptr i64, ptr %p, i64 98
  %v98 = call i64 @llvm.ia64.ld8.s(ptr %a98)
  %a99 = getelementptr i64, ptr %p, i64 99
  %v99 = call i64 @llvm.ia64.ld8.s(ptr %a99)
  %a100 = getelementptr i64, ptr %p, i64 100
  %v100 = call i64 @llvm.ia64.ld8.s(ptr %a100)
  %a101 = getelementptr i64, ptr %p, i64 101
  %v101 = call i64 @llvm.ia64.ld8.s(ptr %a101)
  %a102 = getelementptr i64, ptr %p, i64 102
  %v102 = call i64 @llvm.ia64.ld8.s(ptr %a102)
  %a103 = getelementptr i64, ptr %p, i64 103
  %v103 = call i64 @llvm.ia64.ld8.s(ptr %a103)
  %a104 = getelementptr i64, ptr %p, i64 104
  %v104 = call i64 @llvm.ia64.ld8.s(ptr %a104)
  %a105 = getelementptr i64, ptr %p, i64 105
  %v105 = call i64 @llvm.ia64.ld8.s(ptr %a105)
  %a106 = getelementptr i64, ptr %p, i64 106
  %v106 = call i64 @llvm.ia64.ld8.s(ptr %a106)
  %a107 = getelementptr i64, ptr %p, i64 107
  %v107 = call i64 @llvm.ia64.ld8.s(ptr %a107)
  %a108 = getelementptr i64, ptr %p, i64 108
  %v108 = call i64 @llvm.ia64.ld8.s(ptr %a108)
  %a109 = getelementptr i64, ptr %p, i64 109
  %v109 = call i64 @llvm.ia64.ld8.s(ptr %a109)
  %a110 = getelementptr i64, ptr %p, i64 110
  %v110 = call i64 @llvm.ia64.ld8.s(ptr %a110)
  %a111 = getelementptr i64, ptr %p, i64 111
  %v111 = call i64 @llvm.ia64.ld8.s(ptr %a111)
  %a112 = getelementptr i64, ptr %p, i64 112
  %v112 = call i64 @llvm.ia64.ld8.s(ptr %a112)
  %a113 = getelementptr i64, ptr %p, i64 113
  %v113 = call i64 @llvm.ia64.ld8.s(ptr %a113)
  %a114 = getelementptr i64, ptr %p, i64 114
  %v114 = call i64 @llvm.ia64.ld8.s(ptr %a114)
  %a115 = getelementptr i64, ptr %p, i64 115
  %v115 = call i64 @llvm.ia64.ld8.s(ptr %a115)
  %a116 = getelementptr i64, ptr %p, i64 116
  %v116 = call i64 @llvm.ia64.ld8.s(ptr %a116)
  %a117 = getelementptr i64, ptr %p, i64 117
  %v117 = call i64 @llvm.ia64.ld8.s(ptr %a117)
  %a118 = getelementptr i64, ptr %p, i64 118
  %v118 = call i64 @llvm.ia64.ld8.s(ptr %a118)
  %a119 = getelementptr i64, ptr %p, i64 119
  %v119 = call i64 @llvm.ia64.ld8.s(ptr %a119)
  store volatile i64 %v119, ptr %q
  store volatile i64 %v118, ptr %q
  store volatile i64 %v117, ptr %q
  store volatile i64 %v116, ptr %q
  store volatile i64 %v115, ptr %q
  store volatile i64 %v114, ptr %q
  store volatile i64 %v113, ptr %q
  store volatile i64 %v112, ptr %q
  store volatile i64 %v111, ptr %q
  store volatile i64 %v110, ptr %q
  store volatile i64 %v109, ptr %q
  store volatile i64 %v108, ptr %q
  store volatile i64 %v107, ptr %q
  store volatile i64 %v106, ptr %q
  store volatile i64 %v105, ptr %q
  store volatile i64 %v104, ptr %q
  store volatile i64 %v103, ptr %q
  store volatile i64 %v102, ptr %q
  store volatile i64 %v101, ptr %q
  store volatile i64 %v100, ptr %q
  store volatile i64 %v99, ptr %q
  store volatile i64 %v98, ptr %q
  store volatile i64 %v97, ptr %q
  store volatile i64 %v96, ptr %q
  store volatile i64 %v95, ptr %q
  store volatile i64 %v94, ptr %q
  store volatile i64 %v93, ptr %q
  store volatile i64 %v92, ptr %q
  store volatile i64 %v91, ptr %q
  store volatile i64 %v90, ptr %q
  store volatile i64 %v89, ptr %q
  store volatile i64 %v88, ptr %q
  store volatile i64 %v87, ptr %q
  store volatile i64 %v86, ptr %q
  store volatile i64 %v85, ptr %q
  store volatile i64 %v84, ptr %q
  store volatile i64 %v83, ptr %q
  store volatile i64 %v82, ptr %q
  store volatile i64 %v81, ptr %q
  store volatile i64 %v80, ptr %q
  store volatile i64 %v79, ptr %q
  store volatile i64 %v78, ptr %q
  store volatile i64 %v77, ptr %q
  store volatile i64 %v76, ptr %q
  store volatile i64 %v75, ptr %q
  store volatile i64 %v74, ptr %q
  store volatile i64 %v73, ptr %q
  store volatile i64 %v72, ptr %q
  store volatile i64 %v71, ptr %q
  store volatile i64 %v70, ptr %q
  store volatile i64 %v69, ptr %q
  store volatile i64 %v68, ptr %q
  store volatile i64 %v67, ptr %q
  store volatile i64 %v66, ptr %q
  store volatile i64 %v65, ptr %q
  store volatile i64 %v64, ptr %q
  store volatile i64 %v63, ptr %q
  store volatile i64 %v62, ptr %q
  store volatile i64 %v61, ptr %q
  store volatile i64 %v60, ptr %q
  store volatile i64 %v59, ptr %q
  store volatile i64 %v58, ptr %q
  store volatile i64 %v57, ptr %q
  store volatile i64 %v56, ptr %q
  store volatile i64 %v55, ptr %q
  store volatile i64 %v54, ptr %q
  store volatile i64 %v53, ptr %q
  store volatile i64 %v52, ptr %q
  store volatile i64 %v51, ptr %q
  store volatile i64 %v50, ptr %q
  store volatile i64 %v49, ptr %q
  store volatile i64 %v48, ptr %q
  store volatile i64 %v47, ptr %q
  store volatile i64 %v46, ptr %q
  store volatile i64 %v45, ptr %q
  store volatile i64 %v44, ptr %q
  store volatile i64 %v43, ptr %q
  store volatile i64 %v42, ptr %q
  store volatile i64 %v41, ptr %q
  store volatile i64 %v40, ptr %q
  store volatile i64 %v39, ptr %q
  store volatile i64 %v38, ptr %q
  store volatile i64 %v37, ptr %q
  store volatile i64 %v36, ptr %q
  store volatile i64 %v35, ptr %q
  store volatile i64 %v34, ptr %q
  store volatile i64 %v33, ptr %q
  store volatile i64 %v32, ptr %q
  store volatile i64 %v31, ptr %q
  store volatile i64 %v30, ptr %q
  store volatile i64 %v29, ptr %q
  store volatile i64 %v28, ptr %q
  store volatile i64 %v27, ptr %q
  store volatile i64 %v26, ptr %q
  store volatile i64 %v25, ptr %q
  store volatile i64 %v24, ptr %q
  store volatile i64 %v23, ptr %q
  store volatile i64 %v22, ptr %q
  store volatile i64 %v21, ptr %q
  store volatile i64 %v20, ptr %q
  store volatile i64 %v19, ptr %q
  store volatile i64 %v18, ptr %q
  store volatile i64 %v17, ptr %q
  store volatile i64 %v16, ptr %q
  store volatile i64 %v15, ptr %q
  store volatile i64 %v14, ptr %q
  store volatile i64 %v13, ptr %q
  store volatile i64 %v12, ptr %q
  store volatile i64 %v11, ptr %q
  store volatile i64 %v10, ptr %q
  store volatile i64 %v9, ptr %q
  store volatile i64 %v8, ptr %q
  store volatile i64 %v7, ptr %q
  store volatile i64 %v6, ptr %q
  store volatile i64 %v5, ptr %q
  store volatile i64 %v4, ptr %q
  store volatile i64 %v3, ptr %q
  store volatile i64 %v2, ptr %q
  store volatile i64 %v1, ptr %q
  store volatile i64 %v0, ptr %q
  %t = call i64 @llvm.ia64.tnat(i64 %v0)
  ret i64 %t
}

declare void @use(ptr)
