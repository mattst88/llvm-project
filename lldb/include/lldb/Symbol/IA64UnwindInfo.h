//===-- IA64UnwindInfo.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_IA64UNWINDINFO_H
#define LLDB_SYMBOL_IA64UNWINDINFO_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/CallFrameInfo.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-private.h"

#include <mutex>
#include <set>
#include <string>

namespace lldb_private {

/// eRegisterKindEHFrame register numbers used by the IA-64 unwind plans.
/// These are the DWARF register numbers, plus gdb's numbers for registers
/// that have no DWARF number: pr (all predicates), ip and cfm.
namespace ia64_eh_frame {
enum : uint32_t {
  r0 = 0,
  sp = 12,
  f0 = 128,
  b0 = 320,
  pr = 330,
  ip = 331,
  psr = 332,
  cfm = 333,
  ar0 = 334,
  bsp = ar0 + 17,
  bspstore = ar0 + 18,
  rnat = ar0 + 19,
  unat = ar0 + 36,
  fpsr = ar0 + 40,
  pfs = ar0 + 64,
  lc = ar0 + 65,
};
} // namespace ia64_eh_frame

/// Reads the unwind information defined by the Itanium Software Conventions
/// and Runtime Architecture Guide: the unwind table in .IA_64.unwind, and the
/// unwind descriptors in .IA_64.unwind_info that give the location of each
/// preserved register at every instruction of a function.
///
/// An IA-64 frame has two parts. The memory stack part uses the caller's sp
/// as the CFA, as on other targets. The register stack part uses the
/// caller's bsp (the address of the caller's r32) as the AFA. The size of
/// the caller's register frame is in its CFM, which is not known until the
/// caller's frame is unwound, so the plan does not locate the caller's
/// stacked registers; the IA-64 Architecture plugin adds them.
class IA64UnwindInfo : public CallFrameInfo {
public:
  IA64UnwindInfo(ObjectFile &objfile, lldb::SectionSP unwind_sp,
                 lldb::SectionSP unwind_info_sp);

  bool GetAddressRange(Address addr, AddressRange &range) override;

  std::unique_ptr<UnwindPlan> GetUnwindPlan(llvm::ArrayRef<AddressRange> ranges,
                                            const Address &addr) override;

  std::unique_ptr<UnwindPlan> GetUnwindPlan(const Address &addr) override;

  /// Fills \p row (eRegisterKindEHFrame numbering) for a function that has
  /// saved nothing and has no memory stack frame, e.g. at function entry or
  /// in a leaf function.
  static void DescribeUnsavedFrame(UnwindPlan::Row &row);

  /// Converts instruction slot number \p slot (three slots per bundle) to the
  /// offset lldb uses for it: the bundle offset with the slot number in the
  /// low bits.
  static uint64_t SlotToOffset(uint64_t slot) {
    return (slot / 3) * 16 + slot % 3;
  }

private:
  struct Entry {
    lldb::addr_t start;
    lldb::addr_t end;
    lldb::addr_t info;
  };

  /// Finds the unwind table entry for the function containing \p file_addr.
  bool FindEntry(lldb::addr_t file_addr, Entry &entry);

  /// Stores a DWARF expression for the lifetime of this object, which
  /// outlives the plans that reference it.
  llvm::ArrayRef<uint8_t> Intern(std::string expression);

  ObjectFile &m_objfile;
  lldb::SectionSP m_unwind_sp;
  lldb::SectionSP m_unwind_info_sp;
  DataExtractor m_unwind_data;
  DataExtractor m_unwind_info_data;
  // Base for the unwind info pointer in each table entry: the start of the
  // segment containing the table.
  lldb::addr_t m_info_base = LLDB_INVALID_ADDRESS;
  std::mutex m_mutex;
  std::set<std::string> m_expressions;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_IA64UNWINDINFO_H
