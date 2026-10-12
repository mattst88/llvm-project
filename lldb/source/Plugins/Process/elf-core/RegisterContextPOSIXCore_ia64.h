//===-- RegisterContextPOSIXCore_ia64.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_IA64_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_IA64_H

#include "Plugins/Process/Utility/RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/DataExtractor.h"

#include <cstdint>
#include <memory>
#include <vector>

/// Reads IA-64 registers out of a core file.
///
/// The static registers come from the NT_PRSTATUS note. The stacked
/// registers (r32 and up) are not in the note: the kernel flushes the
/// register stack to the backing store before dumping, so they are read from
/// the core file's memory.
class RegisterContextCorePOSIX_ia64 : public lldb_private::RegisterContext {
public:
  RegisterContextCorePOSIX_ia64(
      lldb_private::Thread &thread,
      lldb_private::RegisterInfoInterface *register_info,
      const lldb_private::DataExtractor &gpregset);

  void InvalidateAllRegisters() override {}

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

private:
  bool ReadNote(uint32_t lldb_reg, uint64_t &value);

  std::unique_ptr<lldb_private::RegisterInfoInterface> m_register_info_up;
  lldb_private::DataExtractor m_gpr;
  /// The register numbers making up the one set, which the set points at.
  std::vector<uint32_t> m_reg_nums;
  lldb_private::RegisterSet m_reg_set;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_REGISTERCONTEXTPOSIXCORE_IA64_H
