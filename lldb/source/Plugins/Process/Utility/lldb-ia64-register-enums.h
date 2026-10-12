//===-- lldb-ia64-register-enums.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_IA64_REGISTER_ENUMS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_IA64_REGISTER_ENUMS_H

namespace lldb_private {

// Internal codes for the IA-64 registers (RegisterKind == eRegisterKindLLDB).
// rN is gpr_r0_ia64 + N; r32 and up are the stacked registers.
enum {
  k_first_gpr_ia64 = 0,
  gpr_r0_ia64 = k_first_gpr_ia64,
  gpr_r1_ia64 = gpr_r0_ia64 + 1,
  gpr_r8_ia64 = gpr_r0_ia64 + 8,
  gpr_r12_ia64 = gpr_r0_ia64 + 12,
  gpr_r13_ia64 = gpr_r0_ia64 + 13,
  gpr_r31_ia64 = gpr_r0_ia64 + 31,
  gpr_r32_ia64 = gpr_r0_ia64 + 32,
  gpr_r127_ia64 = gpr_r0_ia64 + 127,
  gpr_b0_ia64,
  gpr_b7_ia64 = gpr_b0_ia64 + 7,
  gpr_pr_ia64,
  gpr_ip_ia64,
  gpr_cfm_ia64,
  gpr_psr_ia64,
  gpr_rsc_ia64,
  gpr_bsp_ia64,
  gpr_bspstore_ia64,
  gpr_rnat_ia64,
  gpr_ccv_ia64,
  gpr_unat_ia64,
  gpr_fpsr_ia64,
  gpr_pfs_ia64,
  gpr_lc_ia64,
  gpr_ec_ia64,
  gpr_csd_ia64,
  gpr_ssd_ia64,
  gpr_nat_ia64,
  k_last_gpr_ia64 = gpr_nat_ia64,

  k_num_registers_ia64,
  k_num_gpr_registers_ia64 = k_last_gpr_ia64 - k_first_gpr_ia64 + 1,
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_IA64_REGISTER_ENUMS_H
