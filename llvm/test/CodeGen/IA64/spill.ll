; RUN: llc -mtriple=ia64-unknown-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; More values at once than there are registers: 23 scratch ones, 86 inputs
; and locals, eight outputs, which come after the locals, and the four
; preserved ones, which have to be saved first. Two values go to the frame,
; and their sixteen bytes are the scratch area the caller provides. Memory is addressed through a register, so each store to the
; frame needs its address in r2 first, while a load can put the address in
; the register it is about to fill.

define void @spill(ptr %p, ptr %q) {
; CHECK-LABEL: spill:
; CHECK:         alloc r118 = ar.pfs, 2, 85, 8, 0
; CHECK-NEXT:    adds r12 = -32, r12 ;;
; CHECK-NEXT:    adds r2 = 40, r12 ;;
; CHECK-NEXT:    st8 [r2] = r4
; CHECK-NEXT:    adds r2 = 32, r12 ;;
; CHECK-NEXT:    st8 [r2] = r5
; CHECK-NEXT:    adds r2 = 24, r12 ;;
; CHECK-NEXT:    st8 [r2] = r6
; CHECK-NEXT:    adds r2 = 16, r12 ;;
; CHECK-NEXT:    st8 [r2] = r7
; CHECK-NEXT:    ld8 r14 = [r32]
; CHECK-NEXT:    mov r2 = r12 ;;
; CHECK-NEXT:    st8 [r2] = r14
; CHECK:         ld8 r117 = [r32]
; CHECK-NEXT:    ld8 r119 = [r32]
; CHECK:         ld8 r126 = [r32]
; CHECK-NEXT:    ld8 r4 = [r32]
; CHECK:         ld8 r7 = [r32]
; CHECK-NEXT:    ld8 r14 = [r32]
; CHECK-NEXT:    adds r2 = 8, r12 ;;
; CHECK-NEXT:    st8 [r2] = r14
; CHECK-NEXT:    ld8 r32 = [r32]
; CHECK-NEXT:    mov r14 = r12 ;;
; CHECK-NEXT:    ld8 r14 = [r14] ;;
; CHECK-NEXT:    st8 [r33] = r14
; CHECK:         st8 [r33] = r7{{$}}
; CHECK-NEXT:    adds r14 = 8, r12 ;;
; CHECK-NEXT:    ld8 r14 = [r14] ;;
; CHECK-NEXT:    st8 [r33] = r14
; CHECK-NEXT:    st8 [r33] = r32
; CHECK-NEXT:    adds r7 = 16, r12 ;;
; CHECK-NEXT:    ld8 r7 = [r7]
; CHECK:         adds r4 = 40, r12 ;;
; CHECK-NEXT:    ld8 r4 = [r4]
; CHECK-NEXT:    adds r12 = 32, r12
; CHECK-NEXT:    br.ret.sptk.many b0 ;;
  %v0 = load volatile i64, ptr %p
  %v1 = load volatile i64, ptr %p
  %v2 = load volatile i64, ptr %p
  %v3 = load volatile i64, ptr %p
  %v4 = load volatile i64, ptr %p
  %v5 = load volatile i64, ptr %p
  %v6 = load volatile i64, ptr %p
  %v7 = load volatile i64, ptr %p
  %v8 = load volatile i64, ptr %p
  %v9 = load volatile i64, ptr %p
  %v10 = load volatile i64, ptr %p
  %v11 = load volatile i64, ptr %p
  %v12 = load volatile i64, ptr %p
  %v13 = load volatile i64, ptr %p
  %v14 = load volatile i64, ptr %p
  %v15 = load volatile i64, ptr %p
  %v16 = load volatile i64, ptr %p
  %v17 = load volatile i64, ptr %p
  %v18 = load volatile i64, ptr %p
  %v19 = load volatile i64, ptr %p
  %v20 = load volatile i64, ptr %p
  %v21 = load volatile i64, ptr %p
  %v22 = load volatile i64, ptr %p
  %v23 = load volatile i64, ptr %p
  %v24 = load volatile i64, ptr %p
  %v25 = load volatile i64, ptr %p
  %v26 = load volatile i64, ptr %p
  %v27 = load volatile i64, ptr %p
  %v28 = load volatile i64, ptr %p
  %v29 = load volatile i64, ptr %p
  %v30 = load volatile i64, ptr %p
  %v31 = load volatile i64, ptr %p
  %v32 = load volatile i64, ptr %p
  %v33 = load volatile i64, ptr %p
  %v34 = load volatile i64, ptr %p
  %v35 = load volatile i64, ptr %p
  %v36 = load volatile i64, ptr %p
  %v37 = load volatile i64, ptr %p
  %v38 = load volatile i64, ptr %p
  %v39 = load volatile i64, ptr %p
  %v40 = load volatile i64, ptr %p
  %v41 = load volatile i64, ptr %p
  %v42 = load volatile i64, ptr %p
  %v43 = load volatile i64, ptr %p
  %v44 = load volatile i64, ptr %p
  %v45 = load volatile i64, ptr %p
  %v46 = load volatile i64, ptr %p
  %v47 = load volatile i64, ptr %p
  %v48 = load volatile i64, ptr %p
  %v49 = load volatile i64, ptr %p
  %v50 = load volatile i64, ptr %p
  %v51 = load volatile i64, ptr %p
  %v52 = load volatile i64, ptr %p
  %v53 = load volatile i64, ptr %p
  %v54 = load volatile i64, ptr %p
  %v55 = load volatile i64, ptr %p
  %v56 = load volatile i64, ptr %p
  %v57 = load volatile i64, ptr %p
  %v58 = load volatile i64, ptr %p
  %v59 = load volatile i64, ptr %p
  %v60 = load volatile i64, ptr %p
  %v61 = load volatile i64, ptr %p
  %v62 = load volatile i64, ptr %p
  %v63 = load volatile i64, ptr %p
  %v64 = load volatile i64, ptr %p
  %v65 = load volatile i64, ptr %p
  %v66 = load volatile i64, ptr %p
  %v67 = load volatile i64, ptr %p
  %v68 = load volatile i64, ptr %p
  %v69 = load volatile i64, ptr %p
  %v70 = load volatile i64, ptr %p
  %v71 = load volatile i64, ptr %p
  %v72 = load volatile i64, ptr %p
  %v73 = load volatile i64, ptr %p
  %v74 = load volatile i64, ptr %p
  %v75 = load volatile i64, ptr %p
  %v76 = load volatile i64, ptr %p
  %v77 = load volatile i64, ptr %p
  %v78 = load volatile i64, ptr %p
  %v79 = load volatile i64, ptr %p
  %v80 = load volatile i64, ptr %p
  %v81 = load volatile i64, ptr %p
  %v82 = load volatile i64, ptr %p
  %v83 = load volatile i64, ptr %p
  %v84 = load volatile i64, ptr %p
  %v85 = load volatile i64, ptr %p
  %v86 = load volatile i64, ptr %p
  %v87 = load volatile i64, ptr %p
  %v88 = load volatile i64, ptr %p
  %v89 = load volatile i64, ptr %p
  %v90 = load volatile i64, ptr %p
  %v91 = load volatile i64, ptr %p
  %v92 = load volatile i64, ptr %p
  %v93 = load volatile i64, ptr %p
  %v94 = load volatile i64, ptr %p
  %v95 = load volatile i64, ptr %p
  %v96 = load volatile i64, ptr %p
  %v97 = load volatile i64, ptr %p
  %v98 = load volatile i64, ptr %p
  %v99 = load volatile i64, ptr %p
  %v100 = load volatile i64, ptr %p
  %v101 = load volatile i64, ptr %p
  %v102 = load volatile i64, ptr %p
  %v103 = load volatile i64, ptr %p
  %v104 = load volatile i64, ptr %p
  %v105 = load volatile i64, ptr %p
  %v106 = load volatile i64, ptr %p
  %v107 = load volatile i64, ptr %p
  %v108 = load volatile i64, ptr %p
  %v109 = load volatile i64, ptr %p
  %v110 = load volatile i64, ptr %p
  %v111 = load volatile i64, ptr %p
  %v112 = load volatile i64, ptr %p
  %v113 = load volatile i64, ptr %p
  %v114 = load volatile i64, ptr %p
  %v115 = load volatile i64, ptr %p
  %v116 = load volatile i64, ptr %p
  %v117 = load volatile i64, ptr %p
  %v118 = load volatile i64, ptr %p
  %v119 = load volatile i64, ptr %p
  %v120 = load volatile i64, ptr %p
  store volatile i64 %v0, ptr %q
  store volatile i64 %v1, ptr %q
  store volatile i64 %v2, ptr %q
  store volatile i64 %v3, ptr %q
  store volatile i64 %v4, ptr %q
  store volatile i64 %v5, ptr %q
  store volatile i64 %v6, ptr %q
  store volatile i64 %v7, ptr %q
  store volatile i64 %v8, ptr %q
  store volatile i64 %v9, ptr %q
  store volatile i64 %v10, ptr %q
  store volatile i64 %v11, ptr %q
  store volatile i64 %v12, ptr %q
  store volatile i64 %v13, ptr %q
  store volatile i64 %v14, ptr %q
  store volatile i64 %v15, ptr %q
  store volatile i64 %v16, ptr %q
  store volatile i64 %v17, ptr %q
  store volatile i64 %v18, ptr %q
  store volatile i64 %v19, ptr %q
  store volatile i64 %v20, ptr %q
  store volatile i64 %v21, ptr %q
  store volatile i64 %v22, ptr %q
  store volatile i64 %v23, ptr %q
  store volatile i64 %v24, ptr %q
  store volatile i64 %v25, ptr %q
  store volatile i64 %v26, ptr %q
  store volatile i64 %v27, ptr %q
  store volatile i64 %v28, ptr %q
  store volatile i64 %v29, ptr %q
  store volatile i64 %v30, ptr %q
  store volatile i64 %v31, ptr %q
  store volatile i64 %v32, ptr %q
  store volatile i64 %v33, ptr %q
  store volatile i64 %v34, ptr %q
  store volatile i64 %v35, ptr %q
  store volatile i64 %v36, ptr %q
  store volatile i64 %v37, ptr %q
  store volatile i64 %v38, ptr %q
  store volatile i64 %v39, ptr %q
  store volatile i64 %v40, ptr %q
  store volatile i64 %v41, ptr %q
  store volatile i64 %v42, ptr %q
  store volatile i64 %v43, ptr %q
  store volatile i64 %v44, ptr %q
  store volatile i64 %v45, ptr %q
  store volatile i64 %v46, ptr %q
  store volatile i64 %v47, ptr %q
  store volatile i64 %v48, ptr %q
  store volatile i64 %v49, ptr %q
  store volatile i64 %v50, ptr %q
  store volatile i64 %v51, ptr %q
  store volatile i64 %v52, ptr %q
  store volatile i64 %v53, ptr %q
  store volatile i64 %v54, ptr %q
  store volatile i64 %v55, ptr %q
  store volatile i64 %v56, ptr %q
  store volatile i64 %v57, ptr %q
  store volatile i64 %v58, ptr %q
  store volatile i64 %v59, ptr %q
  store volatile i64 %v60, ptr %q
  store volatile i64 %v61, ptr %q
  store volatile i64 %v62, ptr %q
  store volatile i64 %v63, ptr %q
  store volatile i64 %v64, ptr %q
  store volatile i64 %v65, ptr %q
  store volatile i64 %v66, ptr %q
  store volatile i64 %v67, ptr %q
  store volatile i64 %v68, ptr %q
  store volatile i64 %v69, ptr %q
  store volatile i64 %v70, ptr %q
  store volatile i64 %v71, ptr %q
  store volatile i64 %v72, ptr %q
  store volatile i64 %v73, ptr %q
  store volatile i64 %v74, ptr %q
  store volatile i64 %v75, ptr %q
  store volatile i64 %v76, ptr %q
  store volatile i64 %v77, ptr %q
  store volatile i64 %v78, ptr %q
  store volatile i64 %v79, ptr %q
  store volatile i64 %v80, ptr %q
  store volatile i64 %v81, ptr %q
  store volatile i64 %v82, ptr %q
  store volatile i64 %v83, ptr %q
  store volatile i64 %v84, ptr %q
  store volatile i64 %v85, ptr %q
  store volatile i64 %v86, ptr %q
  store volatile i64 %v87, ptr %q
  store volatile i64 %v88, ptr %q
  store volatile i64 %v89, ptr %q
  store volatile i64 %v90, ptr %q
  store volatile i64 %v91, ptr %q
  store volatile i64 %v92, ptr %q
  store volatile i64 %v93, ptr %q
  store volatile i64 %v94, ptr %q
  store volatile i64 %v95, ptr %q
  store volatile i64 %v96, ptr %q
  store volatile i64 %v97, ptr %q
  store volatile i64 %v98, ptr %q
  store volatile i64 %v99, ptr %q
  store volatile i64 %v100, ptr %q
  store volatile i64 %v101, ptr %q
  store volatile i64 %v102, ptr %q
  store volatile i64 %v103, ptr %q
  store volatile i64 %v104, ptr %q
  store volatile i64 %v105, ptr %q
  store volatile i64 %v106, ptr %q
  store volatile i64 %v107, ptr %q
  store volatile i64 %v108, ptr %q
  store volatile i64 %v109, ptr %q
  store volatile i64 %v110, ptr %q
  store volatile i64 %v111, ptr %q
  store volatile i64 %v112, ptr %q
  store volatile i64 %v113, ptr %q
  store volatile i64 %v114, ptr %q
  store volatile i64 %v115, ptr %q
  store volatile i64 %v116, ptr %q
  store volatile i64 %v117, ptr %q
  store volatile i64 %v118, ptr %q
  store volatile i64 %v119, ptr %q
  store volatile i64 %v120, ptr %q
  ret void
}
