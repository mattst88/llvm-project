//===-- IA64UnwindInfo.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/IA64UnwindInfo.h"

#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <climits>
#include <cstring>

using namespace lldb;
using namespace lldb_private;
using namespace llvm::dwarf;

namespace {

/// Registers tracked by the unwind descriptors: the preserved registers, plus
/// psp, the previous stack pointer (the caller's sp).
enum Register {
  kRP,
  kPFS,
  kPSP,
  kPR,
  kUNAT,
  kLC,
  kFPSR,
  kPriUNAT,
  kBSP,
  kBSPStore,
  kRNAT,
  kR4,
  kB1 = kR4 + 4,
  kF2 = kB1 + 5,
  kF16 = kF2 + 4,
  kNumRegisters = kF16 + 16
};

enum Where : uint8_t {
  kNowhere,   // Not saved: still in the register.
  kGR,        // In general register Value.
  kBR,        // In branch register Value.
  kFR,        // In floating-point register Value.
  kSPRel,     // In memory, Value bytes above the stack pointer.
  kPSPRel,    // In memory, Value bytes above the previous stack pointer.
  kSpillHome, // In the spill area, offset not yet assigned.
  kGRSave,    // In the next GR of the prologue's grsave sequence.
};

constexpr int32_t kNever = INT32_MAX;

struct Save {
  Where where;
  int32_t when; // The slot of the instruction that saves the register.
  int64_t value;

  bool operator==(const Save &rhs) const {
    return where == rhs.where && value == rhs.value;
  }
};

/// Location of each tracked register at one point in a function. For kPSP,
/// kNowhere means a fixed-size frame whose size is in value.
struct State {
  std::array<Save, kNumRegisters> reg;

  bool operator==(const State &rhs) const { return reg == rhs.reg; }
};

/// Parses the unwind descriptors of one function. They divide it into
/// prologue and body regions; for a prologue they give the slot at which
/// each preserved register is saved, and the save location. Times are
/// instruction slot numbers (three per bundle) from the function start.
class Parser {
public:
  /// \p slots is the number of slots the function has, which its regions
  /// are kept to. \p target is the slot whose region to stop at, and
  /// \p reached the slot control is at: what an instruction at or after it
  /// saves is not saved. \p times, if there is one, is given every slot at
  /// which the state may change.
  Parser(llvm::ArrayRef<uint8_t> data, int32_t slots, int32_t target,
         int32_t reached, std::set<int32_t> *times)
      : m_data(data), m_slots(slots), m_target(target), m_reached(reached),
        m_times(times) {
    for (Save &s : m_curr.reg)
      s = {kNowhere, kNever, 0};
  }

  bool Run(int64_t stop_label, unsigned copy_depth);
  void Finish();
  const State &GetState() const { return m_curr; }

private:
  // Each prologue pushes its entry state for an epilogue to restore.
  static constexpr unsigned kMaxDepth = 8;
  // copy_state reads the descriptors again, up to the label_state it names,
  // which may come after a copy_state of its own.
  static constexpr unsigned kMaxCopyDepth = 4;

  bool StartRegion(bool body, uint64_t rlen, unsigned mask, unsigned grsave);
  void FinishPrologue();
  bool PrologueDescriptor(uint8_t b);
  bool BodyDescriptor(uint8_t b, int64_t stop_label, unsigned copy_depth,
                      bool &stopped);
  bool SpillDescriptor(uint8_t b);
  void AllocSpillArea(int64_t &offset, int size, int lo, int hi);

  uint8_t Get8() {
    if (m_pos < m_data.size())
      return m_data[m_pos++];
    m_overrun = true;
    return 0;
  }
  uint64_t GetULEB() {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
      byte = Get8();
      if (shift < 64)
        result |= static_cast<uint64_t>(byte & 0x7f) << shift;
      shift += 7;
    } while ((byte & 0x80) && !m_overrun);
    return result;
  }

  int32_t RegionEnd() const { return m_region_start + m_region_len - 1; }
  int32_t When(uint64_t t) const {
    if (m_region_len == 0)
      return m_region_start;
    if (t > static_cast<uint64_t>(m_region_len - 1))
      return RegionEnd();
    return m_region_start + static_cast<int32_t>(t);
  }
  void Note(int32_t when) {
    if (m_times && when != kNever)
      m_times->insert(when + 1);
  }
  void Set(int reg, Where where, int32_t when, int64_t value) {
    Save &s = m_curr.reg[reg];
    s.where = where;
    s.value = value;
    if (s.when == kNever)
      s.when = when;
    Note(s.when);
  }
  void SetWhen(int reg, uint64_t t) {
    Save &s = m_curr.reg[reg];
    if (s.where == kNowhere)
      s.where = kGRSave;
    s.when = When(t);
    Note(s.when);
  }
  void SpillHome(int reg) {
    Set(reg, kSpillHome, RegionEnd(), 0);
    m_any_spills = true;
  }
  bool Active(unsigned qp, uint64_t t) const {
    // Predicate values are unknown, so a predicated save is assumed not to
    // have executed.
    return m_reached > When(t) && qp == 0;
  }

  llvm::ArrayRef<uint8_t> m_data;
  size_t m_pos = 0;
  bool m_overrun = false;
  int32_t m_slots;
  int32_t m_target;
  int32_t m_reached;
  std::set<int32_t> *m_times;

  State m_curr;
  State m_stack[kMaxDepth];
  unsigned m_depth = 0;
  int32_t m_region_start = 0;
  int32_t m_region_len = 0;
  // The unclamped region length from the region header; the spill mask size
  // is computed from it.
  uint64_t m_region_rlen = 0;
  bool m_in_body = false;
  bool m_first = true;
  unsigned m_epilogue_count = 0;
  int32_t m_epilogue_start = kNever;
  unsigned m_gr_save = 0;
  bool m_any_spills = false;
  size_t m_imask = 0;
  bool m_has_imask = false;
  int64_t m_spill_offset = 0x10;
};

// The order in which a prologue saves registers to consecutive GRs starting
// at grsave.
constexpr uint8_t kSaveOrder[] = {kRP,   kPFS, kPSP,  kPR,
                                  kUNAT, kLC,  kFPSR, kPriUNAT};

/// Decodes the 7-bit abreg field of the X-format descriptors.
int DecodeRegister(unsigned abreg) {
  if (abreg >= 0x04 && abreg <= 0x07)
    return kR4 + (abreg - 0x04);
  if (abreg >= 0x22 && abreg <= 0x25)
    return kF2 + (abreg - 0x22);
  if (abreg >= 0x30 && abreg <= 0x3f)
    return kF16 + (abreg - 0x30);
  if (abreg >= 0x41 && abreg <= 0x45)
    return kB1 + (abreg - 0x41);
  switch (abreg) {
  case 0x60:
    return kPR;
  case 0x61:
    return kPSP;
  case 0x62:
    return kPriUNAT;
  case 0x63:
    return kRP;
  case 0x64:
    return kBSP;
  case 0x65:
    return kBSPStore;
  case 0x66:
    return kRNAT;
  case 0x67:
    return kUNAT;
  case 0x68:
    return kFPSR;
  case 0x69:
    return kPFS;
  case 0x6a:
    return kLC;
  }
  return -1;
}

bool Parser::StartRegion(bool body, uint64_t rlen, unsigned mask,
                         unsigned grsave) {
  if (!m_first && !m_in_body)
    FinishPrologue();
  m_first = false;
  // Stop if the target slot is in the previous region.
  if (m_target < m_region_start + m_region_len)
    return false;

  for (; m_epilogue_count != 0; --m_epilogue_count)
    if (m_depth != 0)
      m_curr = m_stack[--m_depth];
  m_epilogue_start = kNever;

  // Clamp the region to the end of the function, whatever length it claims.
  m_region_start += m_region_len;
  m_region_rlen = rlen;
  m_region_len = static_cast<int32_t>(std::min<uint64_t>(
      rlen, static_cast<uint64_t>(m_slots - m_region_start)));
  m_in_body = body;
  if (m_times)
    m_times->insert(m_region_start);
  if (body)
    return true;

  if (m_depth == kMaxDepth) {
    for (unsigned i = 1; i != kMaxDepth; ++i)
      m_stack[i - 1] = m_stack[i];
    --m_depth;
  }
  m_stack[m_depth++] = m_curr;
  for (unsigned i = 0; i != 4; ++i)
    if (mask & (8 >> i))
      Set(kSaveOrder[i], kGR, RegionEnd(), grsave++);
  m_gr_save = grsave;
  m_any_spills = false;
  m_has_imask = false;
  m_spill_offset = 0x10;
  return true;
}

void Parser::AllocSpillArea(int64_t &offset, int size, int lo, int hi) {
  for (int reg = hi; reg >= lo; --reg) {
    Save &s = m_curr.reg[reg];
    if (s.where != kSpillHome)
      continue;
    offset -= size;
    s.where = kPSPRel;
    s.value = offset;
  }
}

void Parser::FinishPrologue() {
  // Registers given only a save time are saved in the grsave GRs.
  for (uint8_t reg : kSaveOrder) {
    Save &s = m_curr.reg[reg];
    if (s.where == kGRSave) {
      s.where = kGR;
      s.value = m_gr_save++;
    }
  }

  // The spill mask has two bits per prologue slot saying whether the
  // instruction there spills an FR, a GR or a BR. Registers of each kind are
  // spilled in ascending order.
  if (m_has_imask) {
    int next[3] = {kF2, kR4, kB1};
    const int limit[3] = {kNumRegisters, kB1, kF2};
    uint8_t bits = 0;
    for (int32_t t = 0; t < m_region_len; ++t) {
      if ((t & 3) == 0) {
        const size_t at = m_imask + static_cast<size_t>(t / 4);
        bits = at < m_data.size() ? m_data[at] : 0;
      }
      unsigned kind = (bits >> (2 * (3 - (t & 3)))) & 3;
      if (kind == 0)
        continue;
      for (int &reg = next[kind - 1]; reg < limit[kind - 1];) {
        Save &s = m_curr.reg[reg++];
        if (s.where == kSpillHome) {
          s.when = m_region_start + t;
          Note(s.when);
          break;
        }
      }
    }
  }

  // The spill area ends at spill_base and holds the FRs at the top, then the
  // BRs, then the GRs.
  if (m_any_spills) {
    int64_t offset = m_spill_offset;
    AllocSpillArea(offset, 16, kF2, kNumRegisters - 1);
    AllocSpillArea(offset, 8, kB1, kB1 + 4);
    AllocSpillArea(offset, 8, kR4, kR4 + 3);
  }
}

bool Parser::SpillDescriptor(uint8_t b) {
  unsigned qp = 0;
  uint8_t b1 = Get8();
  uint8_t reg_byte = b1;
  if (b == 0xfb || b == 0xfc) { // X3, X4
    qp = b1 & 0x3f;
    reg_byte = Get8();
  }
  bool x = (b == 0xf9 || b == 0xfb) ? (b1 >> 7) & 1 : (reg_byte >> 7) & 1;
  int reg = DecodeRegister(reg_byte & 0x7f);

  if (b == 0xf9 || b == 0xfb) { // X1, X3: spill_psprel, spill_sprel
    uint64_t t = GetULEB();
    uint64_t off = GetULEB();
    if (reg < 0)
      return false;
    Note(When(t));
    if (!Active(qp, t))
      return true;
    Save &s = m_curr.reg[reg];
    s.where = x ? kSPRel : kPSPRel;
    s.when = When(t);
    s.value = x ? static_cast<int64_t>(4 * off)
                : 0x10 - static_cast<int64_t>(4 * off);
    return true;
  }

  // X2, X4: spill_reg, or restore
  uint8_t ytreg = Get8();
  uint64_t t = GetULEB();
  if (reg < 0)
    return false;
  Note(When(t));
  if (!Active(qp, t))
    return true;
  Save &s = m_curr.reg[reg];
  if (!x && ytreg == 0) {
    s = {kNowhere, kNever, 0};
    return true;
  }
  s.where = x ? kBR : (ytreg & 0x80) ? kFR : kGR;
  s.when = When(t);
  s.value = ytreg & 0x7f;
  return true;
}

bool Parser::PrologueDescriptor(uint8_t b) {
  if ((b & 0xe0) == 0x80) { // P1: br_mem
    for (int i = 0; i != 5; ++i)
      if (b & (1 << i))
        SpillHome(kB1 + i);
    return true;
  }
  if ((b & 0xf0) == 0xa0) { // P2: br_gr
    uint8_t b1 = Get8();
    unsigned mask = ((b & 0xf) << 1) | (b1 >> 7);
    unsigned gr = b1 & 0x7f;
    for (int i = 0; i != 5; ++i)
      if (mask & (1 << i))
        Set(kB1 + i, kGR, RegionEnd(), gr++);
    return true;
  }
  if ((b & 0xf8) == 0xb0) { // P3
    uint8_t b1 = Get8();
    unsigned r = ((b & 7) << 1) | (b1 >> 7);
    unsigned value = b1 & 0x7f;
    static const uint8_t regs[] = {kPSP,  kRP,       kPFS,  kPR,
                                   kUNAT, kLC,       kRP,   kRNAT,
                                   kBSP,  kBSPStore, kFPSR, kPriUNAT};
    if (r >= sizeof(regs))
      return false;
    Set(regs[r], r == 6 ? kBR : kGR, RegionEnd(), value);
    return true;
  }
  if (b == 0xb8) { // P4: spill_mask
    const uint64_t size = m_region_rlen / 4 + (m_region_rlen % 4 != 0);
    if (size > m_data.size() - m_pos)
      return false;
    m_imask = m_pos;
    m_has_imask = true;
    m_pos += static_cast<size_t>(size);
    return true;
  }
  if (b == 0xb9) { // P5: frgr_mem
    uint8_t b1 = Get8();
    uint32_t frmask = (b1 & 0xf) << 16;
    frmask |= static_cast<uint32_t>(Get8()) << 8;
    frmask |= Get8();
    for (int i = 0; i != 4; ++i)
      if ((b1 >> 4) & (1 << i))
        SpillHome(kR4 + i);
    for (int i = 0; i != 20; ++i)
      if (frmask & (1u << i))
        SpillHome(kF2 + i);
    return true;
  }
  if ((b & 0xe0) == 0xc0) { // P6: fr_mem, gr_mem
    for (int i = 0; i != 4; ++i)
      if (b & (1 << i))
        SpillHome(((b & 0x10) ? kR4 : kF2) + i);
    return true;
  }
  if ((b & 0xf0) == 0xe0) { // P7
    unsigned r = b & 0xf;
    uint64_t t = GetULEB();
    int64_t psprel = 0x10 - static_cast<int64_t>(4 * t);
    switch (r) {
    case 0: // mem_stack_f
      Set(kPSP, kNowhere, When(t), static_cast<int64_t>(16 * GetULEB()));
      return true;
    case 1: // mem_stack_v
      m_curr.reg[kPSP].when = When(t);
      Note(When(t));
      return true;
    case 2: // spill_base
      m_spill_offset = psprel;
      return true;
    case 3: // psp_sprel
      Set(kPSP, kSPRel, RegionEnd(), static_cast<int64_t>(4 * t));
      return true;
    }
    static const uint8_t regs[] = {kRP, kPFS, kPR, kLC, kUNAT, kFPSR};
    int reg = regs[(r - 4) / 2];
    if (r & 1)
      Set(reg, kPSPRel, RegionEnd(), psprel);
    else
      SetWhen(reg, t);
    return true;
  }
  if (b == 0xf0) { // P8
    unsigned r = Get8();
    uint64_t t = GetULEB();
    int64_t psprel = 0x10 - static_cast<int64_t>(4 * t);
    int64_t sprel = static_cast<int64_t>(4 * t);
    if (r >= 1 && r <= 6) {
      static const uint8_t regs[] = {kRP, kPFS, kPR, kLC, kUNAT, kFPSR};
      Set(regs[r - 1], kSPRel, RegionEnd(), sprel);
      return true;
    }
    if (r == 19) { // priunat_when_mem: another descriptor says where.
      m_curr.reg[kPriUNAT].when = When(t);
      Note(When(t));
      return true;
    }
    if (r >= 7 && r <= 18) {
      static const uint8_t regs[] = {kBSP, kBSPStore, kRNAT, kPriUNAT};
      int reg = regs[(r - 7) / 3];
      switch ((r - 7) % 3) {
      case 0: // The last of these is priunat_when_gr.
        SetWhen(reg, t);
        break;
      case 1:
        Set(reg, kPSPRel, RegionEnd(), psprel);
        break;
      case 2:
        Set(reg, kSPRel, RegionEnd(), sprel);
        break;
      }
      return true;
    }
    return false;
  }
  if (b == 0xf1) { // P9: gr_gr
    unsigned mask = Get8() & 0xf;
    unsigned gr = Get8() & 0x7f;
    for (int i = 0; i != 4; ++i)
      if (mask & (1 << i))
        Set(kR4 + i, kGR, RegionEnd(), gr++);
    return true;
  }
  // P10 (unwabi) marks a frame with a different ABI, such as the signal
  // trampoline, whose caller's registers are in a sigcontext. No plan is
  // produced for such a frame.
  if (b >= 0xf9 && b <= 0xfc)
    return SpillDescriptor(b);
  return false;
}

bool Parser::BodyDescriptor(uint8_t b, int64_t stop_label, unsigned copy_depth,
                            bool &stopped) {
  bool copy;
  uint64_t label;
  if ((b & 0xc0) == 0x80) { // B1
    copy = b & 0x20;
    label = b & 0x1f;
  } else if ((b & 0xe0) == 0xc0 || b == 0xe0) { // B2, B3: epilogue
    uint64_t t = GetULEB();
    m_epilogue_start = RegionEnd() - static_cast<int32_t>(std::min<uint64_t>(
                                         t, static_cast<uint64_t>(kNever / 2)));
    // At most kMaxDepth states can be popped.
    const uint64_t ecount = b == 0xe0 ? GetULEB() : (b & 0x1f);
    m_epilogue_count =
        static_cast<unsigned>(std::min<uint64_t>(ecount, kMaxDepth)) + 1;
    Note(m_epilogue_start);
    return true;
  } else if (b == 0xf0 || b == 0xf8) { // B4
    copy = b == 0xf8;
    label = GetULEB();
  } else if (b >= 0xf9 && b <= 0xfc) {
    return SpillDescriptor(b);
  } else {
    return false;
  }

  if (!copy) {
    stopped = stop_label >= 0 && label == static_cast<uint64_t>(stop_label);
    return true;
  }
  // The state to copy is the one the descriptors lead to at the label.
  if (copy_depth == kMaxCopyDepth)
    return false;
  Parser labelled(m_data, m_slots, kNever, m_reached, nullptr);
  if (!labelled.Run(static_cast<int64_t>(label), copy_depth + 1))
    return false;
  m_curr = labelled.m_curr;
  m_depth = labelled.m_depth;
  for (unsigned i = 0; i != m_depth; ++i)
    m_stack[i] = labelled.m_stack[i];
  return true;
}

/// Reads the descriptors up to the region of the target, or up to the
/// label_state of \p stop_label if that is not negative.
bool Parser::Run(int64_t stop_label, unsigned copy_depth) {
  while (m_pos < m_data.size()) {
    uint8_t b = Get8();
    // The descriptors are zero-padded to a multiple of 8 bytes; the padding
    // parses as zero-length regions.
    if (b < 0x80) {
      bool body;
      uint64_t rlen;
      unsigned mask = 0, grsave = 0;
      if (b < 0x40) { // R1
        body = b & 0x20;
        rlen = b & 0x1f;
      } else if ((b & 0xf8) == 0x40) { // R2
        uint8_t b1 = Get8();
        body = false;
        mask = ((b & 7) << 1) | (b1 >> 7);
        grsave = b1 & 0x7f;
        rlen = GetULEB();
      } else if ((b & 0xfe) == 0x60) { // R3
        body = b & 1;
        rlen = GetULEB();
      } else {
        return false;
      }
      if (!StartRegion(body, rlen, mask, grsave))
        return stop_label < 0;
      continue;
    }
    if (m_first)
      return false;
    if (!m_in_body) {
      if (!PrologueDescriptor(b))
        return false;
      continue;
    }
    bool stopped = false;
    if (!BodyDescriptor(b, stop_label, copy_depth, stopped))
      return false;
    if (stopped)
      return true;
  }
  if (m_overrun || stop_label >= 0)
    return false;
  if (!m_first && !m_in_body)
    FinishPrologue();
  return true;
}

void Parser::Finish() {
  // Once the epilogue has restored sp, the registers saved in the memory
  // stack frame have been restored too.
  if (m_reached > m_epilogue_start) {
    m_curr.reg[kPSP] = {kNowhere, kNever, 0};
    for (Save &s : m_curr.reg)
      if (s.where == kSPRel || (s.where == kPSPRel && s.value <= 0x10))
        s = {kNowhere, kNever, 0};
  }
  // Saves by instructions that have not executed yet have not happened.
  for (Save &s : m_curr.reg)
    if (s.when >= m_reached)
      s = {kNowhere, kNever, 0};
  // Neither have saves whose location was never assigned.
  for (Save &s : m_curr.reg)
    if (s.where == kSpillHome || s.where == kGRSave)
      s = {kNowhere, kNever, 0};
}

/// Writes DWARF expressions.
class Expression {
public:
  Expression() : m_os(m_bytes) {}

  Expression &Op(uint8_t op) {
    m_os << static_cast<char>(op);
    return *this;
  }
  Expression &Breg(uint32_t reg, int64_t offset) {
    Op(DW_OP_bregx);
    llvm::encodeULEB128(reg, m_os);
    llvm::encodeSLEB128(offset, m_os);
    return *this;
  }
  Expression &ConstU(uint64_t value) {
    Op(DW_OP_constu);
    llvm::encodeULEB128(value, m_os);
    return *this;
  }
  Expression &PlusU(uint64_t value) {
    Op(DW_OP_plus_uconst);
    llvm::encodeULEB128(value, m_os);
    return *this;
  }
  std::string Take() { return std::move(m_os.str()); }

private:
  std::string m_bytes;
  llvm::raw_string_ostream m_os;
};

/// Pushes psp + \p offset.
void PushPSPRelative(Expression &expr, const Save &psp, int64_t offset) {
  switch (psp.where) {
  case kGR:
    expr.Breg(ia64_eh_frame::r0 + psp.value, offset);
    break;
  case kSPRel:
    // psp itself is saved in memory.
    expr.Breg(ia64_eh_frame::sp, psp.value).Op(DW_OP_deref);
    if (offset >= 0)
      expr.PlusU(static_cast<uint64_t>(offset));
    else
      expr.ConstU(static_cast<uint64_t>(-offset)).Op(DW_OP_minus);
    break;
  default:
    expr.Breg(ia64_eh_frame::sp, psp.value + offset);
    break;
  }
}

/// Pushes the memory address of \p save. Returns false if the register is
/// not saved in memory.
bool PushSaveAddress(Expression &expr, const Save &save, const Save &psp) {
  if (save.where == kSPRel) {
    expr.Breg(ia64_eh_frame::sp, save.value);
    return true;
  }
  if (save.where != kPSPRel)
    return false;
  PushPSPRelative(expr, psp, save.value);
  return true;
}

/// Pushes the value of ar.pfs at function entry, which holds the caller's
/// CFM.
void PushPFS(Expression &expr, const Save &pfs, const Save &psp) {
  switch (pfs.where) {
  case kGR:
    expr.Breg(ia64_eh_frame::r0 + pfs.value, 0);
    return;
  case kBR:
    expr.Breg(ia64_eh_frame::b0 + pfs.value, 0);
    return;
  case kSPRel:
  case kPSPRel:
    PushSaveAddress(expr, pfs, psp);
    expr.Op(DW_OP_deref);
    return;
  default:
    expr.Breg(ia64_eh_frame::pfs, 0);
    return;
  }
}

/// The caller's CFM: the low 38 bits (pfm) of the saved ar.pfs. The bits
/// above hold the epilogue count (pec) and privilege level (ppl).
std::string CallerCFMExpression(const Save &pfs, const Save &psp) {
  Expression expr;
  PushPFS(expr, pfs, psp);
  expr.ConstU((UINT64_C(1) << 38) - 1).Op(DW_OP_and);
  return expr.Take();
}

/// The caller's bsp. A function's register frame starts at bsp, with the
/// caller's locals (sol in the caller's CFM), which survive the call, just
/// below it, so the caller's frame starts sol registers lower. Every 64th
/// doubleword of the backing store (address bits 3-8 all set) is a NaT
/// collection, not a register, so add one doubleword for each one crossed.
std::string CallerBSPExpression(const Save &pfs, const Save &psp) {
  Expression expr;
  PushPFS(expr, pfs, psp);
  // sol = (pfs >> 7) & 0x7f
  expr.Op(DW_OP_lit7).Op(DW_OP_shr).ConstU(0x7f).Op(DW_OP_and);
  // slot = (bsp >> 3) & 0x3f; passed = (sol + 62 - slot) / 63
  expr.Op(DW_OP_dup).PlusU(62);
  expr.Breg(ia64_eh_frame::bsp, 0);
  expr.Op(DW_OP_lit3).Op(DW_OP_shr).ConstU(0x3f).Op(DW_OP_and);
  expr.Op(DW_OP_minus).ConstU(63).Op(DW_OP_div);
  // bsp - 8 * (sol + passed)
  expr.Op(DW_OP_plus).Op(DW_OP_lit3).Op(DW_OP_shl);
  expr.Breg(ia64_eh_frame::bsp, 0);
  expr.Op(DW_OP_swap).Op(DW_OP_minus);
  return expr.Take();
}

/// Maps a tracked register to its eRegisterKindEHFrame number, or to
/// LLDB_INVALID_REGNUM if its caller value is of no use.
uint32_t EHFrameNumber(int reg) {
  if (reg >= kR4 && reg < kB1)
    return ia64_eh_frame::r0 + 4 + (reg - kR4);
  if (reg >= kB1 && reg < kF2)
    return ia64_eh_frame::b0 + 1 + (reg - kB1);
  if (reg >= kF2 && reg < kF16)
    return ia64_eh_frame::f0 + 2 + (reg - kF2);
  if (reg >= kF16 && reg < kNumRegisters)
    return ia64_eh_frame::f0 + 16 + (reg - kF16);
  switch (reg) {
  case kRP:
    return ia64_eh_frame::ip;
  case kPFS:
    return ia64_eh_frame::pfs;
  case kPR:
    return ia64_eh_frame::pr;
  case kUNAT:
    return ia64_eh_frame::unat;
  case kLC:
    return ia64_eh_frame::lc;
  case kFPSR:
    return ia64_eh_frame::fpsr;
  }
  return LLDB_INVALID_REGNUM;
}

using InternFn = llvm::function_ref<llvm::ArrayRef<uint8_t>(std::string)>;

/// Describes \p state in \p row.
void DescribeState(const State &state, UnwindPlan::Row &row, InternFn intern) {
  const Save &psp = state.reg[kPSP];
  const Save &pfs = state.reg[kPFS];

  // The CFA is psp. Unless psp is sp plus a constant, an sp-relative save
  // cannot be expressed as an offset from the CFA.
  const bool fixed = psp.where == kNowhere;
  switch (psp.where) {
  case kGR:
    row.GetCFAValue().SetIsRegisterPlusOffset(ia64_eh_frame::r0 + psp.value, 0);
    break;
  case kSPRel: {
    Expression expr;
    expr.Breg(ia64_eh_frame::sp, psp.value).Op(DW_OP_deref);
    llvm::ArrayRef<uint8_t> bytes = intern(expr.Take());
    row.GetCFAValue().SetIsDWARFExpression(bytes.data(), bytes.size());
    break;
  }
  default:
    row.GetCFAValue().SetIsRegisterPlusOffset(ia64_eh_frame::sp, psp.value);
    break;
  }
  row.SetRegisterLocationToIsCFAPlusOffset(ia64_eh_frame::sp, 0, true);

  llvm::ArrayRef<uint8_t> bsp = intern(CallerBSPExpression(pfs, psp));
  row.GetAFAValue().SetIsDWARFExpression(bsp.data(), bsp.size());
  UnwindPlan::Row::AbstractRegisterLocation loc;
  loc.SetIsAFAPlusOffset(0);
  row.SetRegisterInfo(ia64_eh_frame::bsp, loc);

  llvm::ArrayRef<uint8_t> cfm = intern(CallerCFMExpression(pfs, psp));
  loc.SetIsDWARFExpression(cfm.data(), cfm.size());
  row.SetRegisterInfo(ia64_eh_frame::cfm, loc);

  for (int reg = 0; reg != kNumRegisters; ++reg) {
    const uint32_t num = EHFrameNumber(reg);
    if (num == LLDB_INVALID_REGNUM)
      continue;
    const Save &save = state.reg[reg];
    switch (save.where) {
    case kGR:
      row.SetRegisterLocationToRegister(num, ia64_eh_frame::r0 + save.value,
                                        true);
      break;
    case kBR:
      row.SetRegisterLocationToRegister(num, ia64_eh_frame::b0 + save.value,
                                        true);
      break;
    case kFR:
      row.SetRegisterLocationToRegister(num, ia64_eh_frame::f0 + save.value,
                                        true);
      break;
    case kSPRel:
    case kPSPRel:
      if (fixed) {
        const int64_t offset =
            save.where == kPSPRel ? save.value : save.value - psp.value;
        row.SetRegisterLocationToAtCFAPlusOffset(
            num, static_cast<int32_t>(offset), true);
      } else if (save.where == kPSPRel && psp.where == kGR) {
        row.SetRegisterLocationToAtCFAPlusOffset(
            num, static_cast<int32_t>(save.value), true);
      } else {
        Expression expr;
        PushSaveAddress(expr, save, psp);
        llvm::ArrayRef<uint8_t> bytes = intern(expr.Take());
        loc.SetAtDWARFExpression(bytes.data(), bytes.size());
        row.SetRegisterInfo(num, loc);
      }
      break;
    default:
      // The return address is in b0 until saved; the others are unchanged.
      if (reg == kRP)
        row.SetRegisterLocationToRegister(num, ia64_eh_frame::b0, true);
      else
        row.SetRegisterLocationToSame(num, true);
      break;
    }
  }
}

} // namespace

IA64UnwindInfo::IA64UnwindInfo(ObjectFile &objfile, SectionSP unwind_sp,
                               SectionSP unwind_info_sp)
    : m_objfile(objfile), m_unwind_sp(std::move(unwind_sp)),
      m_unwind_info_sp(std::move(unwind_info_sp)) {
  m_objfile.ReadSectionData(m_unwind_sp.get(), m_unwind_data);
  m_objfile.ReadSectionData(m_unwind_info_sp.get(), m_unwind_info_data);
  // The sections of a linked file are inside its segments.
  SectionSP segment_sp = m_unwind_sp;
  while (SectionSP parent_sp = segment_sp->GetParent())
    segment_sp = parent_sp;
  if (segment_sp != m_unwind_sp)
    m_info_base = segment_sp->GetFileAddress();
}

bool IA64UnwindInfo::FindEntry(addr_t file_addr, Entry &entry) {
  if (m_info_base == LLDB_INVALID_ADDRESS)
    return false;
  // Function addresses are relative to the start of their own segment,
  // which need not be the segment containing the table.
  SectionList *sections = m_objfile.GetSectionList();
  if (!sections)
    return false;
  addr_t text_base = LLDB_INVALID_ADDRESS;
  for (const SectionSP &segment_sp : *sections) {
    if (segment_sp->GetChildren().GetSize() != 0 &&
        segment_sp->ContainsFileAddress(file_addr)) {
      text_base = segment_sp->GetFileAddress();
      break;
    }
  }
  if (text_base == LLDB_INVALID_ADDRESS)
    return false;

  // Entries are sorted by function address.
  const addr_t offset = file_addr - text_base;
  uint64_t low = 0;
  uint64_t high = m_unwind_data.GetByteSize() / 24;
  while (low < high) {
    const uint64_t mid = low + (high - low) / 2;
    lldb::offset_t at = mid * 24;
    const uint64_t start = m_unwind_data.GetU64(&at);
    const uint64_t end = m_unwind_data.GetU64(&at);
    if (offset < start) {
      high = mid;
    } else if (offset >= end) {
      low = mid + 1;
    } else {
      entry.start = text_base + start;
      entry.end = text_base + end;
      entry.info = m_info_base + m_unwind_data.GetU64(&at);
      return true;
    }
  }
  return false;
}

llvm::ArrayRef<uint8_t> IA64UnwindInfo::Intern(std::string expression) {
  std::lock_guard<std::mutex> guard(m_mutex);
  const std::string &kept = *m_expressions.insert(std::move(expression)).first;
  return llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(kept.data()),
                                 kept.size());
}

bool IA64UnwindInfo::GetAddressRange(Address addr, AddressRange &range) {
  range.Clear();
  if (addr.GetModule().get() != m_objfile.GetModule().get())
    return false;
  Entry entry;
  if (!FindEntry(addr.GetFileAddress(), entry))
    return false;
  SectionList *sections = m_objfile.GetSectionList();
  if (!range.GetBaseAddress().ResolveAddressUsingFileSections(entry.start,
                                                              sections))
    return false;
  range.SetByteSize(entry.end - entry.start);
  return true;
}

std::unique_ptr<UnwindPlan> IA64UnwindInfo::GetUnwindPlan(const Address &addr) {
  return GetUnwindPlan({AddressRange(addr, 1)}, addr);
}

std::unique_ptr<UnwindPlan>
IA64UnwindInfo::GetUnwindPlan(llvm::ArrayRef<AddressRange> ranges,
                              const Address &addr) {
  AddressRange range;
  Entry entry;
  if (!GetAddressRange(addr, range) || !FindEntry(addr.GetFileAddress(), entry))
    return nullptr;

  // The unwind info starts with a header doubleword holding the format
  // version, flags and the descriptor length in doublewords.
  const addr_t info_start = m_unwind_info_sp->GetFileAddress();
  if (entry.info < info_start)
    return nullptr;
  lldb::offset_t at = entry.info - info_start;
  if (!m_unwind_info_data.ValidOffsetForDataOfSize(at, 8))
    return nullptr;
  const uint64_t header = m_unwind_info_data.GetU64(&at);
  const uint64_t size = (header & 0xffffffff) * 8;
  if ((header >> 48) != 1 ||
      !m_unwind_info_data.ValidOffsetForDataOfSize(at, size))
    return nullptr;
  llvm::ArrayRef<uint8_t> descriptors(m_unwind_info_data.GetDataStart() + at,
                                      size);

  // Find the slots at which the save state changes, then compute the state
  // at each.
  const int32_t slots = static_cast<int32_t>(std::min<uint64_t>(
      (entry.end - entry.start) / 16 * 3, static_cast<uint64_t>(kNever / 2)));
  std::set<int32_t> times;
  times.insert(0);
  Parser all(descriptors, slots, kNever, kNever, &times);
  if (!all.Run(-1, 0))
    return nullptr;

  auto plan_up = std::make_unique<UnwindPlan>(eRegisterKindEHFrame);
  plan_up->SetSourceName("IA-64 unwind info");
  plan_up->SetSourcedFromCompiler(eLazyBoolYes);
  plan_up->SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  plan_up->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  plan_up->SetReturnAddressRegister(ia64_eh_frame::b0);
  plan_up->SetPlanValidAddressRanges({range});

  auto intern = [this](std::string expression) {
    return Intern(std::move(expression));
  };
  bool any = false;
  State last;
  for (int32_t time : times) {
    if (time < 0 || (time >= slots && any))
      continue;
    Parser parser(descriptors, slots, time, time, nullptr);
    if (!parser.Run(-1, 0))
      return nullptr;
    parser.Finish();
    if (any && parser.GetState() == last)
      continue;
    last = parser.GetState();
    any = true;
    UnwindPlan::Row row;
    row.SetOffset(SlotToOffset(time));
    DescribeState(last, row, intern);
    plan_up->AppendRow(std::move(row));
  }
  return plan_up;
}

void IA64UnwindInfo::DescribeUnsavedFrame(UnwindPlan::Row &row) {
  // These expressions are the same for every function, so they are interned
  // in a static set that is never freed.
  static std::mutex g_mutex;
  static std::set<std::string> g_expressions;
  State state;
  for (Save &s : state.reg)
    s = {kNowhere, kNever, 0};
  DescribeState(state, row, [](std::string expression) {
    std::lock_guard<std::mutex> guard(g_mutex);
    const std::string &kept =
        *g_expressions.insert(std::move(expression)).first;
    return llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(kept.data()), kept.size());
  });
}
