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

#include <cstdint>
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

private:
  std::vector<lldb_private::RegisterInfo> m_register_infos;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTLINUX_IA64_H
