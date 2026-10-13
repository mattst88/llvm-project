//===-- RegisterContextLinux_ia64.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextLinux_ia64.h"
#include "lldb-ia64-register-enums.h"

#include "lldb/Symbol/IA64UnwindInfo.h"
#include "lldb/lldb-defines.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Endian.h"

#include <cmath>
#include <limits>

#include <array>
#include <string>

using namespace lldb;
using namespace lldb_private;

// The register block of an IA-64 NT_PRSTATUS note is ELF_NGREG (128)
// doublewords, of which the kernel fills 57: r0-r31, their NaT bits, pr,
// b0-b7, ip, cfm, psr and twelve application registers. See
// arch/ia64/include/asm/elf.h.
//
// The stacked registers are not in the note; they are in the register
// backing store in memory. They are given offsets past the end of the note
// so that every register has a distinct offset.
static constexpr uint32_t k_note_doublewords = 128;
static constexpr uint32_t k_note_pr = 33;
static constexpr uint32_t k_note_b0 = 34;
static constexpr uint32_t k_note_ip = 42;

// DWARF register numbers, which follow gcc: GRs are 0-127, BRs 320-327 and
// application register N is 334 + N. Each predicate has its own number but
// pr as a whole has none, and ip, cfm and psr have none. These match
// IA64RegisterInfo.td.
//
// The plans built from the unwind table still need to name those four, and
// use eRegisterKindEHFrame numbers, which match the DWARF numbers for every
// other register.
static constexpr uint32_t k_dwarf_b0 = ia64_eh_frame::b0;
static constexpr uint32_t k_dwarf_ar0 = ia64_eh_frame::ar0;

namespace {
struct ApplicationRegister {
  const char *Name;
  uint32_t LLDBNum;
  // Its application register number, or -1.
  int ARNum;
  // Its unwind plan register number, if it has no DWARF number.
  uint32_t EHFrameNum = LLDB_INVALID_REGNUM;
};
} // namespace

// The registers that follow b7 in the note, in its order.
static constexpr ApplicationRegister g_after_b7[] = {
    {"ip", gpr_ip_ia64, -1, ia64_eh_frame::ip},
    {"cfm", gpr_cfm_ia64, -1, ia64_eh_frame::cfm},
    {"psr", gpr_psr_ia64, -1, ia64_eh_frame::psr},
    {"rsc", gpr_rsc_ia64, 16},
    {"bsp", gpr_bsp_ia64, 17},
    {"bspstore", gpr_bspstore_ia64, 18},
    {"rnat", gpr_rnat_ia64, 19},
    {"ccv", gpr_ccv_ia64, 32},
    {"unat", gpr_unat_ia64, 36},
    {"fpsr", gpr_fpsr_ia64, 40},
    {"pfs", gpr_pfs_ia64, 64},
    {"lc", gpr_lc_ia64, 65},
    {"ec", gpr_ec_ia64, 66},
    {"csd", gpr_csd_ia64, 25},
    {"ssd", gpr_ssd_ia64, 26},
};

static const char *GeneralRegisterName(uint32_t num) {
  static const std::array<std::string, 128> names = [] {
    std::array<std::string, 128> result;
    for (uint32_t i = 0; i != 128; ++i)
      result[i] = "r" + std::to_string(i);
    return result;
  }();
  return names[num].c_str();
}

static RegisterInfo MakeInfo(const char *name, const char *alt_name,
                             uint32_t doubleword, uint32_t dwarf,
                             uint32_t generic, uint32_t lldb_num,
                             uint32_t eh_frame = LLDB_INVALID_REGNUM) {
  if (eh_frame == LLDB_INVALID_REGNUM)
    eh_frame = dwarf;
  return {name,
          alt_name,
          8,
          doubleword * 8,
          eEncodingUint,
          eFormatHex,
          {eh_frame, dwarf, generic, LLDB_INVALID_REGNUM, lldb_num},
          nullptr,
          nullptr,
          nullptr};
}

RegisterContextLinux_ia64::RegisterContextLinux_ia64(
    const ArchSpec &target_arch)
    : RegisterInfoInterface(target_arch) {
  m_register_infos.resize(k_num_registers_ia64);

  // r0 reads as zero. r1 is gp, r12 is sp and r13 is tp. r32 and up are the
  // register stack frame, and the first eight of those are the arguments.
  for (uint32_t i = 0; i != 128; ++i) {
    const char *alt_name = nullptr;
    uint32_t generic = LLDB_INVALID_REGNUM;
    if (i == 1) {
      alt_name = "gp";
    } else if (i == 12) {
      alt_name = "sp";
      generic = LLDB_REGNUM_GENERIC_SP;
    } else if (i == 13) {
      alt_name = "tp";
      generic = LLDB_REGNUM_GENERIC_TP;
    } else if (i >= 32 && i < 40) {
      generic = LLDB_REGNUM_GENERIC_ARG1 + (i - 32);
    }
    const uint32_t doubleword = i < 32 ? i : k_note_doublewords + (i - 32);
    m_register_infos[gpr_r0_ia64 + i] =
        MakeInfo(GeneralRegisterName(i), alt_name, doubleword, i, generic,
                 gpr_r0_ia64 + i);
  }

  // b0 holds the return address.
  static const char *const branch_names[] = {"b0", "b1", "b2", "b3",
                                             "b4", "b5", "b6", "b7"};
  for (uint32_t i = 0; i != 8; ++i)
    m_register_infos[gpr_b0_ia64 + i] = MakeInfo(
        branch_names[i], i == 0 ? "rp" : nullptr, k_note_b0 + i, k_dwarf_b0 + i,
        i == 0 ? LLDB_REGNUM_GENERIC_RA : LLDB_INVALID_REGNUM, gpr_b0_ia64 + i);

  m_register_infos[gpr_pr_ia64] =
      MakeInfo("pr", nullptr, k_note_pr, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM, gpr_pr_ia64, ia64_eh_frame::pr);

  for (const auto &[index, reg] : llvm::enumerate(g_after_b7)) {
    const bool is_ip = reg.LLDBNum == gpr_ip_ia64;
    m_register_infos[reg.LLDBNum] =
        MakeInfo(reg.Name, is_ip ? "pc" : nullptr, k_note_ip + index,
                 reg.ARNum < 0 ? LLDB_INVALID_REGNUM : k_dwarf_ar0 + reg.ARNum,
                 is_ip ? LLDB_REGNUM_GENERIC_PC : LLDB_INVALID_REGNUM,
                 reg.LLDBNum, reg.EHFrameNum);
  }

  // Bit N is the NaT bit of rN. The note has a word of NaT bits for r0-r31.
  // Those of the stacked registers are in the backing store alongside the
  // registers; they are merged here so that one register holds all of them.
  // It is placed after every other register.
  m_register_infos[gpr_nat_ia64] = {
      "nat",
      nullptr,
      16,
      (k_note_doublewords + (128 - 32)) * 8 +
          static_cast<uint32_t>(GetFPRSize()) + 128 * 8,
      eEncodingVector,
      eFormatHex,
      {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
       LLDB_INVALID_REGNUM, gpr_nat_ia64},
      nullptr,
      nullptr,
      nullptr};

  // An FR is 82 bits, saved as 16 bytes; the NT_FPREGSET note holds 128 of
  // them. lldb has no type for this format, so each FR is exposed as two
  // registers. fN is the value as a double, which is what variables that
  // debug info places in the register are read from, and is exact for float
  // and double. fN_raw is the raw bytes.
  m_fpr_names.reserve(128 * 2);
  const uint32_t raw_offset = (k_note_doublewords + (128 - 32)) * 8;
  const uint32_t value_offset = raw_offset + GetFPRSize();
  for (uint32_t i = 0; i != 128; ++i) {
    m_fpr_names.push_back("f" + std::to_string(i));
    m_register_infos[fpr_f0_ia64 + i] = {
        m_fpr_names.back().c_str(),
        nullptr,
        8,
        value_offset + i * 8,
        eEncodingIEEE754,
        eFormatFloat,
        {ia64_eh_frame::f0 + i, ia64_eh_frame::f0 + i, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, fpr_f0_ia64 + i},
        nullptr,
        nullptr,
        nullptr};
    m_fpr_names.push_back("f" + std::to_string(i) + "_raw");
    m_register_infos[fpr_f0_raw_ia64 + i] = {
        m_fpr_names.back().c_str(),
        nullptr,
        k_fpr_bytes,
        raw_offset + i * k_fpr_bytes,
        eEncodingVector,
        eFormatHex,
        {ia64_eh_frame::f0_raw + i, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
         LLDB_INVALID_REGNUM, fpr_f0_raw_ia64 + i},
        nullptr,
        nullptr,
        nullptr};
  }
}

size_t RegisterContextLinux_ia64::GetGPRSize() const {
  return k_note_doublewords * 8;
}

double
RegisterContextLinux_ia64::FloatingPointValue(llvm::ArrayRef<uint8_t> raw) {
  if (raw.size() < k_fpr_bytes)
    return std::numeric_limits<double>::quiet_NaN();

  // The spill format is a 64-bit significand with an explicit integer bit,
  // then a 17-bit exponent biased by 65535, then the sign.
  const uint64_t significand = llvm::support::endian::read64le(raw.data());
  const uint32_t high = llvm::support::endian::read32le(raw.data() + 8);
  const uint32_t exponent = high & 0x1ffff;
  const bool negative = (high >> 17) & 1;

  double value;
  if (exponent == 0x1ffff) {
    // An infinity has a significand of 1.0 (only the integer bit set);
    // anything else is a NaN.
    value = significand == UINT64_C(1) << 63
                ? std::numeric_limits<double>::infinity()
                : std::numeric_limits<double>::quiet_NaN();
  } else if (exponent == 0x1fffe && significand == 0) {
    // NaTVal, as left by a failed speculative load.
    return std::numeric_limits<double>::quiet_NaN();
  } else if (significand == 0) {
    value = 0.0;
  } else {
    // A zero exponent denotes a double-extended denormal.
    const int unbiased = exponent == 0 ? -16382 : int(exponent) - 65535;
    value = std::ldexp(static_cast<double>(significand), unbiased - 63);
  }
  return negative ? -value : value;
}

const RegisterInfo *RegisterContextLinux_ia64::GetRegisterInfo() const {
  return m_register_infos.data();
}

uint32_t RegisterContextLinux_ia64::GetRegisterCount() const {
  return static_cast<uint32_t>(m_register_infos.size());
}

uint64_t RegisterContextLinux_ia64::SkipBackingStoreRegisters(uint64_t addr,
                                                              int64_t count) {
  // The last doubleword of every 64 in the backing store (address bits 3-8
  // all set) is a NaT collection holding the NaT bits of the 63 registers
  // before it. So count registers from the start of addr's 64-doubleword
  // group, and add one doubleword per 63 registers.
  const uint64_t base = addr & ~UINT64_C(0x1ff);
  const int64_t position = static_cast<int64_t>((addr >> 3) & 0x3f) + count;
  int64_t groups = position / 63;
  int64_t rest = position % 63;
  if (rest < 0) {
    rest += 63;
    --groups;
  }
  return base + static_cast<uint64_t>(groups * 64 + rest) * 8;
}
