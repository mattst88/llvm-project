//===-- ArchitectureIA64.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/IA64/ArchitectureIA64.h"
#include "Plugins/Process/Utility/RegisterContextLinux_ia64.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/IA64UnwindInfo.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContextUnwind.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnwindLLDB.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/RegisterValue.h"

using namespace lldb_private;
using namespace lldb;

LLDB_PLUGIN_DEFINE(ArchitectureIA64)

void ArchitectureIA64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "IA-64-specific algorithms",
                                &ArchitectureIA64::Create);
}

void ArchitectureIA64::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitectureIA64::Create);
}

std::unique_ptr<Architecture> ArchitectureIA64::Create(const ArchSpec &arch) {
  if (arch.GetMachine() != llvm::Triple::ia64)
    return nullptr;
  return std::unique_ptr<Architecture>(new ArchitectureIA64());
}

addr_t ArchitectureIA64::GetBytesBeforeInstruction(const Address &addr) const {
  const addr_t file_addr = addr.GetFileAddress();
  if (file_addr == LLDB_INVALID_ADDRESS)
    return 0;
  const addr_t slot = file_addr & 0xf;
  return slot < 3 ? slot : 0;
}

UnwindPlanSP ArchitectureIA64::GetArchitectureUnwindPlan(
    Thread &thread, RegisterContextUnwind *regctx,
    std::shared_ptr<const UnwindPlan> current_unwindplan) {
  // Only plans that give the caller's bsp as the AFA can be extended, and
  // those are the ones that use eRegisterKindEHFrame.
  if (!current_unwindplan ||
      current_unwindplan->GetRegisterKind() != eRegisterKindEHFrame)
    return {};

  // Reads the caller's value of a register, as given by the plan.
  auto read_callers = [&](uint32_t eh_frame_num, uint64_t &value) -> bool {
    uint32_t lldb_num;
    if (!regctx->ConvertBetweenRegisterKinds(eRegisterKindEHFrame, eh_frame_num,
                                             eRegisterKindLLDB, lldb_num))
      return false;
    const RegisterInfo *reg_info = regctx->GetRegisterInfoAtIndex(lldb_num);
    UnwindLLDB::ConcreteRegisterLocation regloc = {};
    if (!reg_info || regctx->SavedLocationForRegister(lldb_num, regloc) !=
                         UnwindLLDB::RegisterSearchResult::eRegisterFound)
      return false;
    RegisterValue reg_value;
    if (!regctx->ReadRegisterValueFromRegisterLocation(regloc, reg_info,
                                                       reg_value))
      return false;
    value = reg_value.GetAsUInt64();
    return true;
  };

  uint64_t cfm, bsp;
  if (!read_callers(ia64_eh_frame::cfm, cfm) ||
      !read_callers(ia64_eh_frame::bsp, bsp))
    return {};

  // r32 is at bsp and the rest of the frame follows, skipping any NaT
  // collection doublewords in between.
  const uint64_t size_of_frame = cfm & 0x7f;
  UnwindPlanSP new_plan = std::make_shared<UnwindPlan>(*current_unwindplan);
  new_plan->SetSourceName((current_unwindplan->GetSourceName().GetStringRef() +
                           " with the caller's stacked registers")
                              .str()
                              .c_str());

  const int row_count = current_unwindplan->GetRowCount();
  for (int i = 0; i < row_count; i++) {
    UnwindPlan::Row row = *current_unwindplan->GetRowAtIndex(i);
    for (uint64_t n = 0; n != size_of_frame; ++n) {
      const uint64_t addr =
          RegisterContextLinux_ia64::SkipBackingStoreRegisters(bsp, n);
      UnwindPlan::Row::AbstractRegisterLocation loc;
      loc.SetAtAFAPlusOffset(static_cast<int32_t>(addr - bsp));
      row.SetRegisterInfo(ia64_eh_frame::r0 + 32 + n, loc);
    }
    new_plan->InsertRow(std::move(row), /*replace_existing=*/true);
  }
  return new_plan;
}
