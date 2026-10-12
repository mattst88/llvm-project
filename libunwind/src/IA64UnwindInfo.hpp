//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Parses the unwind descriptors of the Itanium Software Conventions and
//  Runtime Architecture Guide.
//
//===----------------------------------------------------------------------===//

#ifndef __IA64_UNWIND_INFO_HPP__
#define __IA64_UNWIND_INFO_HPP__

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "libunwind.h"

namespace libunwind {

/// A function's unwind information is a list of descriptors that divide it
/// into prologue and body regions. For a prologue they give the slot at which
/// each callee-saved register is saved, and where. Time is measured in
/// instruction slots (three per bundle) from the start of the function.
template <typename A> class IA64UnwindInfo {
public:
  typedef typename A::pint_t pint_t;

  /// The tracked registers: the callee-saved registers and the previous stack
  /// pointer (psp), i.e. the caller's SP.
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
    kGRSave,    // In the next of the prologue's grsave registers.
  };

  static constexpr int32_t kNever = INT32_MAX;

  struct Save {
    Where where;
    int32_t when; // The slot of the instruction that saves the register.
    int64_t value;
  };

  /// Where each register is saved at a given point in a function. For kPSP,
  /// kNowhere means a fixed-size frame, and Value is its size.
  struct State {
    Save reg[kNumRegisters];
  };

  /// Finds where the registers are saved at slot \p target of the function
  /// whose descriptors are in [\p begin, \p end). Predicated descriptors are
  /// evaluated against the predicate registers \p pr. Returns false if the
  /// descriptors are malformed.
  static bool parse(A &addressSpace, pint_t begin, pint_t end, int32_t target,
                    uint64_t pr, State &result);

private:
  struct Parser {
    Parser(A &addressSpace, pint_t begin, pint_t end, int32_t target,
           int32_t reached, uint64_t pr)
        : addressSpace(addressSpace), begin(begin), end(end), target(target),
          reached(reached), pr(pr) {
      for (Save &s : curr.reg)
        s = {kNowhere, kNever, 0};
    }

    // Each prologue pushes the state it starts from, for an epilogue to pop.
    static constexpr unsigned kMaxDepth = 8;
    // copy_state re-reads the descriptors up to the named label_state, which
    // may itself follow another copy_state.
    static constexpr unsigned kMaxCopyDepth = 4;

    bool run(int64_t stopLabel, unsigned copyDepth);
    void finish();

    bool startRegion(bool body, uint64_t rlen, unsigned mask, unsigned grsave);
    void finishPrologue();
    bool prologueDescriptor(uint8_t b, pint_t &p);
    bool bodyDescriptor(uint8_t b, pint_t &p, int64_t stopLabel,
                        unsigned copyDepth, bool &stopped);
    bool spillDescriptor(uint8_t b, pint_t &p);
    void allocSpillArea(int64_t &offset, int size, int lo, int hi);

    int32_t regionEnd() const { return regionStart + regionLen - 1; }
    int32_t when(uint64_t t) const {
      if (regionLen > 0 && t > static_cast<uint64_t>(regionLen - 1))
        return regionEnd();
      return regionStart + static_cast<int32_t>(t);
    }
    void set(int reg, Where where, int32_t when, int64_t value) {
      Save &s = curr.reg[reg];
      s.where = where;
      s.value = value;
      if (s.when == kNever)
        s.when = when;
    }
    void setWhen(int reg, uint64_t t) {
      Save &s = curr.reg[reg];
      if (s.where == kNowhere)
        s.where = kGRSave;
      s.when = when(t);
    }
    void spillHome(int reg) {
      set(reg, kSpillHome, regionEnd(), 0);
      anySpills = true;
    }
    bool active(unsigned qp, uint64_t t) const {
      return reached > when(t) && (qp == 0 || ((pr >> qp) & 1));
    }
    uint64_t uleb(pint_t &p) { return addressSpace.getULEB128(p, end); }

    A &addressSpace;
    pint_t begin;
    pint_t end;
    int32_t target;  // Stop at the region this slot is in.
    int32_t reached; // The current slot.
    uint64_t pr;

    State curr;
    State stack[kMaxDepth];
    unsigned depth = 0;
    int32_t regionStart = 0;
    int32_t regionLen = 0;
    bool inBody = false;
    bool first = true;
    unsigned epilogueCount = 0;
    int32_t epilogueStart = kNever;
    unsigned grSave = 0;
    bool anySpills = false;
    pint_t imask = 0;
    int64_t spillOffset = 0x10;
  };

  // The registers a prologue saves in consecutive general registers, in save
  // order.
  static constexpr uint8_t kSaveOrder[] = {kRP,   kPFS, kPSP,  kPR,
                                           kUNAT, kLC,  kFPSR, kPriUNAT};

  /// Maps the 7-bit abreg field of the X formats to a tracked register, or -1.
  static int decodeRegister(unsigned abreg) {
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
};

template <typename A>
bool IA64UnwindInfo<A>::Parser::startRegion(bool body, uint64_t rlen,
                                            unsigned mask, unsigned grsave) {
  if (!first && !inBody)
    finishPrologue();
  first = false;
  // The previous region may be the one being looked for.
  if (target < regionStart + regionLen)
    return false;

  for (; epilogueCount != 0; --epilogueCount)
    if (depth != 0)
      curr = stack[--depth];
  epilogueStart = kNever;

  regionStart += regionLen;
  regionLen = static_cast<int32_t>(rlen);
  inBody = body;
  if (body)
    return true;

  if (depth == kMaxDepth) {
    memmove(&stack[0], &stack[1], sizeof(State) * (kMaxDepth - 1));
    --depth;
  }
  stack[depth++] = curr;
  for (unsigned i = 0; i != 4; ++i)
    if (mask & (8 >> i))
      set(kSaveOrder[i], kGR, regionEnd(), grsave++);
  grSave = grsave;
  anySpills = false;
  imask = 0;
  spillOffset = 0x10;
  return true;
}

template <typename A>
void IA64UnwindInfo<A>::Parser::allocSpillArea(int64_t &offset, int size,
                                               int lo, int hi) {
  for (int reg = hi; reg >= lo; --reg) {
    Save &s = curr.reg[reg];
    if (s.where != kSpillHome)
      continue;
    offset -= size;
    s.where = kPSPRel;
    s.value = offset;
  }
}

template <typename A> void IA64UnwindInfo<A>::Parser::finishPrologue() {
  // Registers with only a save time are saved in the prologue's grsave
  // registers.
  for (uint8_t reg : kSaveOrder) {
    Save &s = curr.reg[reg];
    if (s.where == kGRSave) {
      s.where = kGR;
      s.value = grSave++;
    }
  }

  // The imask has two bits per prologue slot, saying whether the instruction
  // there spills a floating-point, general or branch register. Registers of
  // each kind are spilled in ascending order.
  if (imask != 0) {
    int next[3] = {kF2, kR4, kB1};
    const int limit[3] = {kNumRegisters, kB1, kF2};
    uint8_t bits = 0;
    for (int32_t t = 0; t < regionLen; ++t) {
      if ((t & 3) == 0)
        bits = addressSpace.get8(imask + static_cast<pint_t>(t / 4));
      unsigned kind = (bits >> (2 * (3 - (t & 3)))) & 3;
      if (kind == 0)
        continue;
      for (int &reg = next[kind - 1]; reg < limit[kind - 1];) {
        Save &s = curr.reg[reg++];
        if (s.where == kSpillHome) {
          s.when = regionStart + t;
          break;
        }
      }
    }
  }

  // The spill area ends at spill_base and holds the floating-point registers
  // at the top, then the branch registers, then the general registers.
  if (anySpills) {
    int64_t offset = spillOffset;
    allocSpillArea(offset, 16, kF2, kNumRegisters - 1);
    allocSpillArea(offset, 8, kB1, kB1 + 4);
    allocSpillArea(offset, 8, kR4, kR4 + 3);
  }
}

template <typename A>
bool IA64UnwindInfo<A>::Parser::spillDescriptor(uint8_t b, pint_t &p) {
  unsigned qp = 0;
  uint8_t b1 = addressSpace.get8(p++);
  uint8_t regByte = b1;
  if (b == 0xfb || b == 0xfc) { // X3, X4
    qp = b1 & 0x3f;
    regByte = addressSpace.get8(p++);
  }
  bool x = (b == 0xf9 || b == 0xfb) ? (b1 >> 7) & 1 : (regByte >> 7) & 1;
  int reg = decodeRegister(regByte & 0x7f);

  if (b == 0xf9 || b == 0xfb) { // X1, X3: spill_psprel, spill_sprel
    uint64_t t = uleb(p);
    uint64_t off = uleb(p);
    if (reg < 0)
      return false;
    if (!active(qp, t))
      return true;
    Save &s = curr.reg[reg];
    s.where = x ? kSPRel : kPSPRel;
    s.when = when(t);
    s.value = x ? static_cast<int64_t>(4 * off)
                : 0x10 - static_cast<int64_t>(4 * off);
    return true;
  }

  // X2, X4: spill_reg, or restore
  uint8_t ytreg = addressSpace.get8(p++);
  uint64_t t = uleb(p);
  if (reg < 0)
    return false;
  if (!active(qp, t))
    return true;
  Save &s = curr.reg[reg];
  if (!x && ytreg == 0) {
    s = {kNowhere, kNever, 0};
    return true;
  }
  s.where = x ? kBR : (ytreg & 0x80) ? kFR : kGR;
  s.when = when(t);
  s.value = ytreg & 0x7f;
  return true;
}

template <typename A>
bool IA64UnwindInfo<A>::Parser::prologueDescriptor(uint8_t b, pint_t &p) {
  if ((b & 0xe0) == 0x80) { // P1: br_mem
    for (int i = 0; i != 5; ++i)
      if (b & (1 << i))
        spillHome(kB1 + i);
    return true;
  }
  if ((b & 0xf0) == 0xa0) { // P2: br_gr
    uint8_t b1 = addressSpace.get8(p++);
    unsigned mask = ((b & 0xf) << 1) | (b1 >> 7);
    unsigned gr = b1 & 0x7f;
    for (int i = 0; i != 5; ++i)
      if (mask & (1 << i))
        set(kB1 + i, kGR, regionEnd(), gr++);
    return true;
  }
  if ((b & 0xf8) == 0xb0) { // P3
    uint8_t b1 = addressSpace.get8(p++);
    unsigned r = ((b & 7) << 1) | (b1 >> 7);
    unsigned value = b1 & 0x7f;
    static const uint8_t regs[] = {kPSP,  kRP,       kPFS,  kPR,
                                   kUNAT, kLC,       kRP,   kRNAT,
                                   kBSP,  kBSPStore, kFPSR, kPriUNAT};
    if (r >= sizeof(regs))
      return false;
    set(regs[r], r == 6 ? kBR : kGR, regionEnd(), value);
    return true;
  }
  if (b == 0xb8) { // P4: spill_mask
    imask = p;
    p += static_cast<pint_t>((2 * regionLen + 7) / 8);
    return true;
  }
  if (b == 0xb9) { // P5: frgr_mem
    uint8_t b1 = addressSpace.get8(p++);
    uint32_t frmask = (b1 & 0xf) << 16;
    frmask |= static_cast<uint32_t>(addressSpace.get8(p++)) << 8;
    frmask |= addressSpace.get8(p++);
    for (int i = 0; i != 4; ++i)
      if ((b1 >> 4) & (1 << i))
        spillHome(kR4 + i);
    for (int i = 0; i != 20; ++i)
      if (frmask & (1u << i))
        spillHome(kF2 + i);
    return true;
  }
  if ((b & 0xe0) == 0xc0) { // P6: fr_mem, gr_mem
    for (int i = 0; i != 4; ++i)
      if (b & (1 << i))
        spillHome(((b & 0x10) ? kR4 : kF2) + i);
    return true;
  }
  if ((b & 0xf0) == 0xe0) { // P7
    unsigned r = b & 0xf;
    uint64_t t = uleb(p);
    int64_t psprel = 0x10 - static_cast<int64_t>(4 * t);
    switch (r) {
    case 0: // mem_stack_f
      set(kPSP, kNowhere, when(t), static_cast<int64_t>(16 * uleb(p)));
      return true;
    case 1: // mem_stack_v
      curr.reg[kPSP].when = when(t);
      return true;
    case 2: // spill_base
      spillOffset = psprel;
      return true;
    case 3: // psp_sprel
      set(kPSP, kSPRel, regionEnd(), static_cast<int64_t>(4 * t));
      return true;
    }
    static const uint8_t regs[] = {kRP, kPFS, kPR, kLC, kUNAT, kFPSR};
    int reg = regs[(r - 4) / 2];
    if (r & 1)
      set(reg, kPSPRel, regionEnd(), psprel);
    else
      setWhen(reg, t);
    return true;
  }
  if (b == 0xf0) { // P8
    unsigned r = addressSpace.get8(p++);
    uint64_t t = uleb(p);
    int64_t psprel = 0x10 - static_cast<int64_t>(4 * t);
    int64_t sprel = static_cast<int64_t>(4 * t);
    if (r >= 1 && r <= 6) {
      static const uint8_t regs[] = {kRP, kPFS, kPR, kLC, kUNAT, kFPSR};
      set(regs[r - 1], kSPRel, regionEnd(), sprel);
      return true;
    }
    if (r == 19) { // priunat_when_mem; the location is in another descriptor.
      curr.reg[kPriUNAT].when = when(t);
      return true;
    }
    if (r >= 7 && r <= 18) {
      static const uint8_t regs[] = {kBSP, kBSPStore, kRNAT, kPriUNAT};
      int reg = regs[(r - 7) / 3];
      switch ((r - 7) % 3) {
      case 0: // The last of these is priunat_when_gr.
        setWhen(reg, t);
        break;
      case 1:
        set(reg, kPSPRel, regionEnd(), psprel);
        break;
      case 2:
        set(reg, kSPRel, regionEnd(), sprel);
        break;
      }
      return true;
    }
    return false;
  }
  if (b == 0xf1) { // P9: gr_gr
    unsigned mask = addressSpace.get8(p++) & 0xf;
    unsigned gr = addressSpace.get8(p++) & 0x7f;
    for (int i = 0; i != 4; ++i)
      if (mask & (1 << i))
        set(kR4 + i, kGR, regionEnd(), gr++);
    return true;
  }
  if (b == 0xff) { // P10: unwabi, a frame of another ABI
    p += 2;
    return false;
  }
  if (b >= 0xf9 && b <= 0xfc)
    return spillDescriptor(b, p);
  return false;
}

template <typename A>
bool IA64UnwindInfo<A>::Parser::bodyDescriptor(uint8_t b, pint_t &p,
                                               int64_t stopLabel,
                                               unsigned copyDepth,
                                               bool &stopped) {
  bool copy;
  uint64_t label;
  if ((b & 0xc0) == 0x80) { // B1
    copy = b & 0x20;
    label = b & 0x1f;
  } else if ((b & 0xe0) == 0xc0) { // B2: epilogue
    uint64_t t = uleb(p);
    epilogueStart = regionEnd() - static_cast<int32_t>(t);
    epilogueCount = (b & 0x1f) + 1;
    return true;
  } else if (b == 0xe0) { // B3: epilogue
    uint64_t t = uleb(p);
    epilogueStart = regionEnd() - static_cast<int32_t>(t);
    epilogueCount = static_cast<unsigned>(uleb(p)) + 1;
    return true;
  } else if (b == 0xf0 || b == 0xf8) { // B4
    copy = b == 0xf8;
    label = uleb(p);
  } else if (b >= 0xf9 && b <= 0xfc) {
    return spillDescriptor(b, p);
  } else {
    return false;
  }

  if (!copy) {
    stopped = stopLabel >= 0 && label == static_cast<uint64_t>(stopLabel);
    return true;
  }
  // Copy the state the descriptors reach at the label.
  if (copyDepth == kMaxCopyDepth)
    return false;
  Parser labelled(addressSpace, begin, end, kNever, reached, pr);
  if (!labelled.run(static_cast<int64_t>(label), copyDepth + 1))
    return false;
  curr = labelled.curr;
  depth = labelled.depth;
  memcpy(stack, labelled.stack, sizeof(State) * depth);
  return true;
}

/// Reads the descriptors up to the target's region, or up to the label_state
/// for \p stopLabel if it is non-negative.
template <typename A>
bool IA64UnwindInfo<A>::Parser::run(int64_t stopLabel, unsigned copyDepth) {
  pint_t p = begin;
  while (p < end) {
    uint8_t b = addressSpace.get8(p++);
    // The descriptors are zero-padded to a multiple of eight bytes, and the
    // padding decodes as zero-length regions.
    if (b < 0x80) {
      bool body;
      uint64_t rlen;
      unsigned mask = 0, grsave = 0;
      if (b < 0x40) { // R1
        body = b & 0x20;
        rlen = b & 0x1f;
      } else if ((b & 0xf8) == 0x40) { // R2
        uint8_t b1 = addressSpace.get8(p++);
        body = false;
        mask = ((b & 7) << 1) | (b1 >> 7);
        grsave = b1 & 0x7f;
        rlen = uleb(p);
      } else if ((b & 0xfe) == 0x60) { // R3
        body = b & 1;
        rlen = uleb(p);
      } else {
        return false;
      }
      if (!startRegion(body, rlen, mask, grsave))
        return stopLabel < 0;
      continue;
    }
    if (first)
      return false;
    if (!inBody) {
      if (!prologueDescriptor(b, p))
        return false;
      continue;
    }
    bool stopped = false;
    if (!bodyDescriptor(b, p, stopLabel, copyDepth, stopped))
      return false;
    if (stopped)
      return true;
  }
  if (stopLabel >= 0)
    return false;
  if (!first && !inBody)
    finishPrologue();
  return true;
}

template <typename A> void IA64UnwindInfo<A>::Parser::finish() {
  // Once the epilogue has restored SP, everything saved in the frame has been
  // restored too.
  if (reached > epilogueStart) {
    curr.reg[kPSP] = {kNowhere, kNever, 0};
    for (Save &s : curr.reg)
      if (s.where == kSPRel || (s.where == kPSPRel && s.value <= 0x10))
        s = {kNowhere, kNever, 0};
  }
  // A save by an instruction that has not executed yet does not count.
  for (Save &s : curr.reg)
    if (s.when >= reached)
      s = {kNowhere, kNever, 0};
}

template <typename A>
bool IA64UnwindInfo<A>::parse(A &addressSpace, pint_t begin, pint_t end,
                              int32_t target, uint64_t pr, State &result) {
  Parser parser(addressSpace, begin, end, target, target, pr);
  if (!parser.run(-1, 0))
    return false;
  parser.finish();
  result = parser.curr;
  return true;
}

} // namespace libunwind

#endif // __IA64_UNWIND_INFO_HPP__
