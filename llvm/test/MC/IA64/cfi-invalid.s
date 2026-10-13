// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s
// RUN: not llvm-mc -triple ia64 -filetype=obj -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s

// The unwind information of a function is what ".prologue", ".save" and the
// like describe. GNU as has every directive of call frame information as an
// error, and an entry made of them could not name b0, whose number is 320.

	.text
	.proc f
f:
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_startproc
	br.ret.sptk.many b0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_endproc
	.endp f

// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_sections .debug_frame
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_fde_data
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_def_cfa r12, 0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_def_cfa_register r12
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_def_cfa_offset 16
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_adjust_cfa_offset 16
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_offset r4, -8
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_rel_offset r4, 8
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_register r4, r5
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_return_column b0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_restore r4
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_undefined r4
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_same_value r4
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_remember_state
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_restore_state
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_window_save
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_negate_ra_state
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_negate_ra_state_with_pc
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_escape 0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_signal_frame
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_personality 0, f
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_personality_id 0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_lsda 0, f
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_val_encoded_addr r4, 0, f
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_inline_lsda
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_label l
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_val_offset r4, 8

// The ones that only LLVM has, and a name in upper case.
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_llvm_def_aspace_cfa r12, 0, 0
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_b_key_frame
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.cfi_mte_tagged_frame
// CHECK: :[[#@LINE+1]]:2: error: CFI is not supported for this target
	.CFI_STARTPROC

// CHECK-NOT: error:
