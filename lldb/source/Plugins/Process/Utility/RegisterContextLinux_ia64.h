//===-- RegisterContextLinux_ia64.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_IA64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_IA64_H

#include "RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <string>
#include <vector>

class RegisterContextLinux_ia64 : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextLinux_ia64(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  /// Returns the address of the register \p count registers after the one at
  /// \p addr in the register backing store; \p count may be negative.
  static uint64_t SkipBackingStoreRegisters(uint64_t addr, int64_t count);

  /// Size of a saved FR, in memory and in the NT_FPREGSET note.
  static constexpr uint32_t k_fpr_bytes = 16;

  /// Size of the NT_FPREGSET note: f0-f127 in spill format.
  static constexpr size_t GetFPRSize() { return 128 * k_fpr_bytes; }

  /// Returns the value of the FR saved as the 16 bytes \p raw, rounded to a
  /// double.
  static double FloatingPointValue(llvm::ArrayRef<uint8_t> raw);

private:
  // Storage for the FR names referenced by m_register_infos.
  std::vector<std::string> m_fpr_names;
  std::vector<lldb_private::RegisterInfo> m_register_infos;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_IA64_H
