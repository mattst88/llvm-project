//===-- RegisterContextPOSIXCore_ia64.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextPOSIXCore_ia64.h"

#include "Plugins/Process/Utility/RegisterContextLinux_ia64.h"
#include "Plugins/Process/Utility/lldb-ia64-register-enums.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextCorePOSIX_ia64::RegisterContextCorePOSIX_ia64(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset)
    : RegisterContext(thread, 0), m_register_info_up(register_info) {
  m_gpr = gpregset;

  // Every register described belongs to the one set, in order.
  const uint32_t count = m_register_info_up->GetRegisterCount();
  m_reg_nums.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    m_reg_nums.push_back(i);
  m_reg_set = {"General Purpose Registers", "gpr", m_reg_nums.size(),
               m_reg_nums.data()};
}

size_t RegisterContextCorePOSIX_ia64::GetRegisterCount() {
  return m_register_info_up->GetRegisterCount();
}

const RegisterInfo *
RegisterContextCorePOSIX_ia64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg >= GetRegisterCount())
    return nullptr;
  return &m_register_info_up->GetRegisterInfo()[reg];
}

size_t RegisterContextCorePOSIX_ia64::GetRegisterSetCount() { return 1; }

const RegisterSet *RegisterContextCorePOSIX_ia64::GetRegisterSet(size_t set) {
  if (set != 0)
    return nullptr;
  return &m_reg_set;
}

bool RegisterContextCorePOSIX_ia64::ReadNote(uint32_t lldb_reg,
                                             uint64_t &value) {
  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(lldb_reg);
  if (!reg_info)
    return false;
  lldb::offset_t offset = reg_info->byte_offset;
  if (offset + reg_info->byte_size > m_gpr.GetByteSize())
    return false;
  value = m_gpr.GetU64(&offset);
  return true;
}

bool RegisterContextCorePOSIX_ia64::ReadRegister(const RegisterInfo *reg_info,
                                                 RegisterValue &value) {
  if (!reg_info)
    return false;
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  if (reg >= gpr_r32_ia64 && reg <= gpr_r127_ia64) {
    // The low 7 bits of cfm are the frame size (sof): only r32 to
    // r32 + sof - 1 exist. The note's bsp is the end of the backing store,
    // past the frame rather than at its start.
    uint64_t cfm, bsp;
    if (!ReadNote(gpr_cfm_ia64, cfm) || !ReadNote(gpr_bsp_ia64, bsp))
      return false;
    const int64_t size_of_frame = cfm & 0x7f;
    const int64_t index = reg - gpr_r32_ia64;
    if (index >= size_of_frame)
      return false;
    const addr_t addr = RegisterContextLinux_ia64::SkipBackingStoreRegisters(
        bsp, index - size_of_frame);
    ProcessSP process_sp = m_thread.GetProcess();
    if (!process_sp)
      return false;
    Status error;
    const uint64_t raw =
        process_sp->ReadUnsignedIntegerFromMemory(addr, 8, 0, error);
    if (error.Fail())
      return false;
    value.SetUInt64(raw);
    return true;
  }

  uint64_t raw;
  if (!ReadNote(reg, raw))
    return false;
  if (reg == gpr_bsp_ia64) {
    // The note's bsp is the end of the backing store, following the kernel's
    // convention: on kernel entry the current frame is flushed after the
    // earlier ones. The program's ar.bsp is where that frame starts, sof
    // registers (plus NaT collections) below the note's bsp.
    uint64_t cfm;
    if (!ReadNote(gpr_cfm_ia64, cfm))
      return false;
    raw = RegisterContextLinux_ia64::SkipBackingStoreRegisters(
        raw, -static_cast<int64_t>(cfm & 0x7f));
  }
  if (reg == gpr_ip_ia64) {
    // The note holds the bundle address here and the slot number in psr.ri
    // (bits 41-42). The instruction address is their sum, which is what line
    // tables and return addresses use.
    uint64_t psr;
    if (!ReadNote(gpr_psr_ia64, psr))
      return false;
    raw += (psr >> 41) & 0x3;
  }
  value.SetUInt64(raw);
  return true;
}

bool RegisterContextCorePOSIX_ia64::WriteRegister(const RegisterInfo *reg_info,
                                                  const RegisterValue &value) {
  // A core file is a snapshot; its registers are not writable.
  return false;
}
