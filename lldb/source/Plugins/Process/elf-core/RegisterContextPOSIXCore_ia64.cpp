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
#include "llvm/BinaryFormat/ELF.h"

#include <cstring>

using namespace lldb;
using namespace lldb_private;

RegisterContextCorePOSIX_ia64::RegisterContextCorePOSIX_ia64(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContext(thread, 0), m_register_info_up(register_info) {
  m_gpr = gpregset;

  // The core file does not identify its OS, so the note cannot be looked up
  // by its OS-specific description.
  for (const CoreNote &note : notes)
    if (note.info.n_type == llvm::ELF::NT_FPREGSET)
      m_fpr = note.data;

  const uint32_t count = m_register_info_up->GetRegisterCount();
  m_reg_nums.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    m_reg_nums.push_back(i);
  m_reg_sets.push_back({"General Purpose Registers", "gpr",
                        k_num_gpr_registers_ia64,
                        &m_reg_nums[k_first_gpr_ia64]});
  if (m_fpr.GetByteSize() >= RegisterContextLinux_ia64::GetFPRSize()) {
    m_reg_sets.push_back({"Floating Point Registers", "fpr",
                          k_num_fpr_registers_ia64,
                          &m_reg_nums[k_first_fpr_ia64]});
    m_reg_sets.push_back({"Floating Point Registers As Saved", "fpr_raw",
                          k_num_fpr_registers_ia64,
                          &m_reg_nums[k_first_fpr_raw_ia64]});
  }
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

size_t RegisterContextCorePOSIX_ia64::GetRegisterSetCount() {
  return m_reg_sets.size();
}

const RegisterSet *RegisterContextCorePOSIX_ia64::GetRegisterSet(size_t set) {
  if (set >= m_reg_sets.size())
    return nullptr;
  return &m_reg_sets[set];
}

bool RegisterContextCorePOSIX_ia64::ReadFloatingPoint(uint32_t num,
                                                      uint8_t *bytes) {
  const uint32_t size = RegisterContextLinux_ia64::k_fpr_bytes;
  if (m_fpr.GetByteSize() < RegisterContextLinux_ia64::GetFPRSize())
    return false;
  if (num < 2) {
    // f0 is 0.0 and f1 is 1.0 regardless of the note; the kernel does not
    // fill them in.
    std::memset(bytes, 0, size);
    if (num == 1) {
      bytes[7] = 0x80;
      bytes[8] = 0xff;
      bytes[9] = 0xff;
    }
    return true;
  }
  return m_fpr.CopyData(num * size, size, bytes) == size;
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

  if (reg >= k_first_fpr_ia64 && reg <= k_last_fpr_raw_ia64) {
    const bool raw = reg >= k_first_fpr_raw_ia64;
    uint8_t bytes[RegisterContextLinux_ia64::k_fpr_bytes];
    if (!ReadFloatingPoint(
            reg - (raw ? k_first_fpr_raw_ia64 : k_first_fpr_ia64), bytes))
      return false;
    if (raw)
      value.SetBytes(bytes, sizeof(bytes), eByteOrderLittle);
    else
      value.SetDouble(RegisterContextLinux_ia64::FloatingPointValue(bytes));
    return true;
  }

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
