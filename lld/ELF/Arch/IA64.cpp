//===- IA64.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// IA-64 (Itanium), little-endian LP64 ABI as used by Linux.
//
// Code is a sequence of 16-byte bundles, each holding a 5-bit template and
// three 41-bit instruction slots. An instruction relocation is at the bundle
// address plus the slot number, and PC-relative values are relative to the
// bundle address.
//
// A module addresses its data relative to gp, which every function expects
// in r1. Data within 2 MiB of gp (short data) is reached with a single add;
// everything else goes through the linkage table, which is itself short data.
// A function must be called with its own module's gp, so a function pointer
// is the address of a function descriptor: the entry point and that gp.
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "RelocScan.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class IA64 final : public TargetInfo {
public:
  IA64(Ctx &);
  uint32_t calcEFlags() const override;
  void initTargetSpecificSections() override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  template <class ELFT, class RelTy>
  void scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                       unsigned shard);
  void scanSection(InputSectionBase &sec, unsigned shard) override;
  void finalizeRelocScan() override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  void scanFptr(RelocScan &rs, InputSectionBase &sec, RelType type,
                uint64_t offset, Symbol &sym, int64_t addend) const;
  void scanIplt(RelocScan &rs, InputSectionBase &sec, RelType type,
                uint64_t offset, Symbol &sym, int64_t addend) const;
};
} // namespace

// The lazy binding stub of a PLT entry follows its import stub.
static constexpr unsigned importStubSize = 32;

IA64::IA64(Ctx &ctx) : TargetInfo(ctx) {
  gotRel = R_IA64_DIR64LSB;
  pltRel = R_IA64_IPLTLSB;
  relativeRel = R_IA64_REL64LSB;
  symbolicRel = R_IA64_DIR64LSB;
  gotEntrySize = 8;
  // An import stub loads a function descriptor. The descriptors are preceded
  // by three words reserved for the dynamic loader, occupying two entries.
  gotPltEntrySize = 16;
  gotPltHeaderEntriesNum = 2;
  pltHeaderSize = 48;
  pltEntrySize = 48;
  ipltEntrySize = 48;
  defaultCommonPageSize = 0x10000;
  defaultMaxPageSize = 0x10000;
  defaultImageBase = 0x4000000000000000;
  // All-zero bytes are break 0 in every slot, wherever in a bundle they fall.
  trapInstr = {0, 0, 0, 0};
}

uint32_t IA64::calcEFlags() const { return EF_IA_64_ABI64; }

void IA64::initTargetSpecificSections() {
  ctx.in.ia64Opd = std::make_unique<IA64OpdSection>(ctx);
  ctx.inputSections.push_back(ctx.in.ia64Opd.get());
  ctx.in.ia64Ltoff = std::make_unique<IA64LtoffSection>(ctx);
  ctx.inputSections.push_back(ctx.in.ia64Ltoff.get());
}

//===----------------------------------------------------------------------===//
// Bundles
//===----------------------------------------------------------------------===//

static const uint64_t slotMask = (uint64_t(1) << 41) - 1;

static uint64_t readSlot(const uint8_t *bundle, unsigned slot) {
  uint64_t lo = read64le(bundle), hi = read64le(bundle + 8);
  switch (slot) {
  case 0:
    return (lo >> 5) & slotMask;
  case 1:
    return ((lo >> 46) | (hi << 18)) & slotMask;
  default:
    return hi >> 23;
  }
}

static void writeSlot(uint8_t *bundle, unsigned slot, uint64_t insn) {
  uint64_t lo = read64le(bundle), hi = read64le(bundle + 8);
  switch (slot) {
  case 0:
    lo = (lo & ~(slotMask << 5)) | (insn << 5);
    break;
  case 1:
    lo = (lo & ~(slotMask << 46)) | (insn << 46);
    hi = (hi & ~(slotMask >> 18)) | (insn >> 18);
    break;
  default:
    hi = (hi & ~(slotMask << 23)) | (insn << 23);
    break;
  }
  write64le(bundle, lo);
  write64le(bundle + 8, hi);
}

// Bits [from, from + width) of v, moved to bit `to` of an instruction.
static uint64_t field(uint64_t v, unsigned from, unsigned width, unsigned to) {
  return ((v >> from) & ((uint64_t(1) << width) - 1)) << to;
}

// Replace the bits of an instruction that `mask` selects.
static void setFields(uint8_t *bundle, unsigned slot, uint64_t mask,
                      uint64_t bits) {
  writeSlot(bundle, slot, (readSlot(bundle, slot) & ~mask) | bits);
}

static uint64_t imm14(uint64_t v) {
  return field(v, 13, 1, 36) | field(v, 7, 6, 27) | field(v, 0, 7, 13);
}
static const uint64_t imm14Mask = imm14(~uint64_t(0));

static uint64_t imm22(uint64_t v) {
  return field(v, 21, 1, 36) | field(v, 16, 5, 22) | field(v, 7, 9, 27) |
         field(v, 0, 7, 13);
}
static const uint64_t imm22Mask = imm22(~uint64_t(0));

// The low bits of a 64-bit immediate, in the X slot of a movl. The L slot
// holds bits 22 to 62.
static uint64_t imm64X(uint64_t v) {
  return field(v, 63, 1, 36) | field(v, 21, 1, 21) | field(v, 16, 5, 22) |
         field(v, 7, 9, 27) | field(v, 0, 7, 13);
}
static const uint64_t imm64XMask = imm64X(~uint64_t(0));

// Branch displacements count bundles.
static uint64_t pcrel21b(uint64_t v) {
  return field(v, 24, 1, 36) | field(v, 4, 20, 13);
}
static const uint64_t pcrel21bMask = pcrel21b(~uint64_t(0));

static uint64_t pcrel21m(uint64_t v) {
  return field(v, 24, 1, 36) | field(v, 11, 13, 20) | field(v, 4, 7, 6);
}
static const uint64_t pcrel21mMask = pcrel21m(~uint64_t(0));

static uint64_t pcrel21f(uint64_t v) {
  return field(v, 24, 1, 36) | field(v, 4, 20, 6);
}
static const uint64_t pcrel21fMask = pcrel21f(~uint64_t(0));

// The X slot of a brl. The L slot holds displacement bits 24-62, starting at
// its bit 2.
static uint64_t pcrel60X(uint64_t v) {
  return field(v, 63, 1, 36) | field(v, 4, 20, 13);
}
static const uint64_t pcrel60XMask = pcrel60X(~uint64_t(0));

static void setImm22(uint8_t *bundle, unsigned slot, uint64_t v) {
  setFields(bundle, slot, imm22Mask, imm22(v));
}

static void setImm64(uint8_t *bundle, uint64_t v) {
  writeSlot(bundle, 1, field(v, 22, 41, 0));
  setFields(bundle, 2, imm64XMask, imm64X(v));
}

static void setPcrel60(uint8_t *bundle, uint64_t v) {
  setFields(bundle, 1, field(~uint64_t(0), 0, 39, 2), field(v, 24, 39, 2));
  setFields(bundle, 2, pcrel60XMask, pcrel60X(v));
}

//===----------------------------------------------------------------------===//
// Relocation scanning
//===----------------------------------------------------------------------===//

// Only used for relocations seen by relocateNonAlloc and the .eh_frame scan.
RelExpr IA64::getRelExpr(RelType type, const Symbol &s,
                         const uint8_t *loc) const {
  switch (type) {
  case R_IA64_NONE:
    return R_NONE;
  case R_IA64_DIR32LSB:
  case R_IA64_DIR64LSB:
    return R_ABS;
  case R_IA64_PCREL32LSB:
  case R_IA64_PCREL64LSB:
    return R_PC;
  case R_IA64_SECREL32LSB:
  case R_IA64_SECREL64LSB:
    return RE_IA64_SECREL;
  case R_IA64_SEGREL32LSB:
  case R_IA64_SEGREL64LSB:
    return RE_IA64_SEGREL;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

RelType IA64::getDynRel(RelType type) const {
  if (type == R_IA64_DIR64LSB)
    return type;
  return R_IA64_NONE;
}

int64_t IA64::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_IA64_NONE:
  // The dynamic loader overwrites both words an IPLT relocation targets and
  // reads neither when the relocation has a symbol.
  case R_IA64_IPLTLSB:
  case R_IA64_FPTR32LSB:
  case R_IA64_FPTR64LSB:
    return 0;
  case R_IA64_DIR64LSB:
  case R_IA64_REL64LSB:
    return read64le(buf);
  default:
    InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
    return 0;
  }
}

static void reportNotWritable(Ctx &ctx, InputSectionBase &sec, RelType type,
                              uint64_t offset, Symbol &sym) {
  auto diag = Err(ctx);
  diag << "relocation " << type << " against ";
  if (sym.getName().empty())
    diag << "local symbol";
  else
    diag << "symbol '" << &sym << "'";
  diag << " needs a dynamic relocation in a read-only section";
  printLocation(diag, sec, sym, offset);
}

// Report a relocation the dynamic loader has no dynamic relocation for.
static void reportNotPic(Ctx &ctx, InputSectionBase &sec, RelType type,
                         uint64_t offset, Symbol &sym) {
  auto diag = Err(ctx);
  diag << "relocation " << type << " cannot be used against ";
  if (sym.getName().empty())
    diag << "local symbol";
  else
    diag << "symbol '" << &sym << "'";
  diag << "; recompile with -fPIC";
  printLocation(diag, sec, sym, offset);
}

// A word holding a function's address, i.e. the address of its function
// descriptor.
void IA64::scanFptr(RelocScan &rs, InputSectionBase &sec, RelType type,
                    uint64_t offset, Symbol &sym, int64_t addend) const {
  bool canWrite = (sec.flags & SHF_WRITE) || !ctx.arg.zText;
  if (isIA64DynamicFdesc(ctx, sym)) {
    // Every module must see the same address, so the dynamic loader creates
    // the descriptor.
    if (addend != 0) {
      auto diag = Err(ctx);
      diag << "relocation " << type << " has a non-zero addend against "
           << "symbol '" << &sym << "'";
      printLocation(diag, sec, sym, offset);
    } else if (type == R_IA64_FPTR64I) {
      reportNotPic(ctx, sec, type, offset, sym);
    } else if (!canWrite) {
      reportNotWritable(ctx, sec, type, offset, sym);
    } else {
      // The loader writes as many bytes as the relocation type specifies.
      std::lock_guard<std::mutex> lock(ctx.relocMutex);
      ctx.in.relaDyn->addSymbolReloc(type, sec, offset, sym);
    }
    return;
  }
  // The address of an undefined weak function is null.
  if (sym.isUndefined()) {
    sec.addReloc({R_ABS, type, offset, addend, &sym});
    return;
  }
  // The load bias can only be applied to a 64-bit word.
  if (!ctx.arg.isPic)
    sec.addReloc({RE_IA64_FDESC, type, offset, addend, &sym});
  else if (type != R_IA64_FPTR64LSB)
    reportNotPic(ctx, sec, type, offset, sym);
  else if (!canWrite)
    reportNotWritable(ctx, sec, type, offset, sym);
  else
    ctx.in.relaDyn->addRelativeReloc<true>(
        relativeRel, sec, offset, sym, addend, type, RE_IA64_FDESC, rs.shard);
}

// An in-place function descriptor: the entry point and the gp of the defining
// module. Such a descriptor is not the function's address, so it need not be
// unique.
void IA64::scanIplt(RelocScan &rs, InputSectionBase &sec, RelType type,
                    uint64_t offset, Symbol &sym, int64_t addend) const {
  bool canWrite = (sec.flags & SHF_WRITE) || !ctx.arg.zText;
  if (sym.isPreemptible) {
    if (addend != 0) {
      auto diag = Err(ctx);
      diag << "relocation " << type << " has a non-zero addend against "
           << "symbol '" << &sym << "'";
      printLocation(diag, sec, sym, offset);
    } else if (!canWrite) {
      reportNotWritable(ctx, sec, type, offset, sym);
    } else {
      std::lock_guard<std::mutex> lock(ctx.relocMutex);
      ctx.in.relaDyn->addSymbolReloc(R_IA64_IPLTLSB, sec, offset, sym);
    }
    return;
  }
  if (sym.isUndefined())
    return;
  Symbol &gp = *ctx.sym.ia64Gp;
  if (!ctx.arg.isPic || isAbsolute(sym)) {
    sec.addReloc({R_ABS, symbolicRel, offset, addend, &sym});
  } else if (!canWrite) {
    reportNotWritable(ctx, sec, type, offset, sym);
    return;
  } else {
    ctx.in.relaDyn->addRelativeReloc<true>(
        relativeRel, sec, offset, sym, addend, symbolicRel, R_ABS, rs.shard);
  }
  if (!ctx.arg.isPic)
    sec.addReloc({R_ABS, symbolicRel, offset + 8, 0, &gp});
  else
    ctx.in.relaDyn->addRelativeReloc<true>(relativeRel, sec, offset + 8, gp, 0,
                                           symbolicRel, R_ABS, rs.shard);
}

template <class ELFT, class RelTy>
void IA64::scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                           unsigned shard) {
  RelocScan rs(ctx, &sec, shard);
  sec.relocations.reserve(rels.size());
  bool canWrite = (sec.flags & SHF_WRITE) || !ctx.arg.zText;

  for (auto it = rels.begin(); it != rels.end(); ++it) {
    RelType type = it->getType(false);
    uint32_t symIdx = it->getSymbol(false);
    Symbol &sym = sec.getFile<ELFT>()->getSymbol(symIdx);
    uint64_t offset = it->r_offset;
    if (sym.isUndefined() && symIdx != 0 &&
        rs.maybeReportUndefined(cast<Undefined>(sym), offset))
      continue;
    // The psABI has no relocation that calls a resolver, and the dynamic
    // loader has no support for one.
    if (LLVM_UNLIKELY(sym.isGnuIFunc())) {
      auto diag = Err(ctx);
      diag << "relocation " << type << " against STT_GNU_IFUNC symbol '" << &sym
           << "', which IA-64 does not support";
      printLocation(diag, sec, sym, offset);
      continue;
    }
    int64_t addend = rs.getAddend<ELFT>(*it, type);
    // An executable referring directly to a shared object's definition. The
    // dynamic loader has no copy relocation, and a function has no address
    // here that other modules would agree on, so only a word the loader can
    // write the address into works.
    auto refusesShared = [&] {
      if (!sym.isShared() || ctx.arg.shared)
        return false;
      if (type == R_IA64_DIR64LSB) {
        if (canWrite)
          return false;
        reportNotWritable(ctx, sec, type, offset, sym);
        return true;
      }
      reportNotPic(ctx, sec, type, offset, sym);
      return true;
    };
    RelExpr expr;
    switch (type) {
    case R_IA64_NONE:
    // Marks the load of what an R_IA64_LTOFF22X fetched the address of, for
    // a linker that turns the pair into an add to gp. This one leaves both
    // as they are.
    case R_IA64_LDXMOV:
      continue;

    // Absolute relocations:
    case R_IA64_IMM14:
    case R_IA64_IMM22:
    case R_IA64_IMM64:
    case R_IA64_DIR32LSB:
    case R_IA64_DIR64LSB:
      expr = R_ABS;
      break;

    // The link-time value, regardless of load address: the dynamic loader
    // uses these to compute the load bias.
    case R_IA64_LTV32LSB:
    case R_IA64_LTV64LSB:
      sec.addReloc({R_ABS, type, offset, addend, &sym});
      continue;

    // PC-relative relocations:
    case R_IA64_PCREL21M:
    case R_IA64_PCREL21F:
    case R_IA64_PCREL22:
    case R_IA64_PCREL64I:
    case R_IA64_PCREL32LSB:
    case R_IA64_PCREL64LSB:
      if (!refusesShared())
        rs.processR_PC(type, offset, addend, sym);
      continue;
    case R_IA64_PCREL21B:
    case R_IA64_PCREL21BI:
    case R_IA64_PCREL60B:
      rs.processR_PLT_PC(type, offset, addend, sym);
      continue;

    // Relative to gp, to the segment or to the section:
    case R_IA64_GPREL22:
    case R_IA64_GPREL64I:
    case R_IA64_GPREL32LSB:
    case R_IA64_GPREL64LSB:
      expr = RE_IA64_GPREL;
      break;
    case R_IA64_SEGREL32LSB:
    case R_IA64_SEGREL64LSB:
      expr = RE_IA64_SEGREL;
      break;
    case R_IA64_SECREL32LSB:
    case R_IA64_SECREL64LSB:
      expr = RE_IA64_SECREL;
      break;

    // Linkage table entries. Each is found again, and made, once the scan is
    // over: see finalizeRelocScan.
    case R_IA64_LTOFF22:
    case R_IA64_LTOFF22X:
    case R_IA64_LTOFF64I:
      sec.addReloc({RE_IA64_LTOFF, type, offset, addend, &sym});
      continue;
    case R_IA64_LTOFF_FPTR22:
    case R_IA64_LTOFF_FPTR64I:
    case R_IA64_LTOFF_FPTR32LSB:
    case R_IA64_LTOFF_FPTR64LSB:
      if (addend != 0 && isIA64DynamicFdesc(ctx, sym)) {
        auto diag = Err(ctx);
        diag << "relocation " << type << " has a non-zero addend against "
             << "symbol '" << &sym << "'";
        printLocation(diag, sec, sym, offset);
        continue;
      }
      sec.addReloc({RE_IA64_LTOFF_FDESC, type, offset, addend, &sym});
      continue;

    // Function descriptors:
    case R_IA64_FPTR64I:
    case R_IA64_FPTR32LSB:
    case R_IA64_FPTR64LSB:
      scanFptr(rs, sec, type, offset, sym, addend);
      continue;
    case R_IA64_IPLTLSB:
      scanIplt(rs, sec, type, offset, sym, addend);
      continue;

    default:
      Err(ctx) << getErrorLoc(ctx, sec.content().data() + offset)
               << "unknown relocation (" << type.v << ") against symbol "
               << &sym;
      continue;
    }
    if (!refusesShared())
      rs.process(expr, type, offset, sym, addend);
  }
}

void IA64::scanSection(InputSectionBase &sec, unsigned shard) {
  elf::scanSection1<IA64, ELF64LE>(*this, sec, shard);
}

// Create the linkage table entries and function descriptors the scan found
// uses for. The scan ran in parallel; walking its results in input order
// keeps entry placement deterministic.
void IA64::finalizeRelocScan() {
  for (ELFFileBase *file : ctx.objectFiles) {
    for (InputSectionBase *sec : file->getSections()) {
      if (!sec || sec == &InputSection::discarded || !sec->isLive())
        continue;
      for (const Relocation &r : sec->relocs()) {
        if (r.expr == RE_IA64_LTOFF)
          ctx.in.ia64Ltoff->addEntry(*r.sym, r.addend, false);
        else if (r.expr == RE_IA64_LTOFF_FDESC)
          ctx.in.ia64Ltoff->addEntry(*r.sym, r.addend, true);
        else if (r.expr == RE_IA64_FDESC)
          ctx.in.ia64Opd->addEntry(*r.sym, r.addend);
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// The procedure linkage table
//===----------------------------------------------------------------------===//

// An .IA_64.pltoff entry is the function descriptor an import stub calls
// through. Until the dynamic loader binds it, it points at the entry's lazy
// binding stub and holds this module's gp.
void IA64::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write64le(buf, s.getPltVA(ctx) + importStubSize);
  write64le(buf + 8, ctx.sym.ia64Gp->getVA(ctx));
}

// Reached from a lazy binding stub with the index of the relocation in r15
// and this module's gp in r14: call the resolver the dynamic loader left in
// the reserved words, with the loader's handle for this module in r16.
void IA64::writePltHeader(uint8_t *buf) const {
  static const uint8_t header[] = {
      0x0b, 0x10, 0x00, 0x1c, 0x00, 0x21, 0xe0, 0x00, // mov r2=r14;;
      0x08, 0x00, 0x48, 0x00, 0x00, 0x00, 0x04, 0x00, // addl r14=reserve,r2;;
      0x0b, 0x80, 0x20, 0x1c, 0x18, 0x14, 0x10, 0x41, // ld8 r16=[r14],8;;
      0x38, 0x30, 0x28, 0x00, 0x00, 0x00, 0x04, 0x00, // ld8 r17=[r14],8;;
      0x11, 0x08, 0x00, 0x1c, 0x18, 0x10, 0x60, 0x88, // ld8 r1=[r14]
      0x04, 0x80, 0x03, 0x00, 0x60, 0x00, 0x80, 0x00, // mov b6=r17; br b6;;
  };
  memcpy(buf, header, sizeof(header));
  setImm22(buf, 1, ctx.in.gotPlt->getVA() - ctx.sym.ia64Gp->getVA(ctx));
}

void IA64::writePlt(uint8_t *buf, const Symbol &sym,
                    uint64_t pltEntryAddr) const {
  static const uint8_t entry[] = {
      // The import stub: call through the .IA_64.pltoff descriptor, leaving
      // this module's gp in r14.
      0x0b,
      0x78,
      0x00,
      0x02,
      0x00,
      0x24,
      0x00,
      0x41, // addl r15=pltoff,r1;;
      0x3c,
      0x30,
      0x28,
      0xc0,
      0x01,
      0x08,
      0x00,
      0x84, // ld8 r16=[r15],8
            // mov r14=r1;;
      0x11,
      0x08,
      0x00,
      0x1e,
      0x18,
      0x10,
      0x60,
      0x80, // ld8 r1=[r15]
      0x04,
      0x80,
      0x03,
      0x00,
      0x60,
      0x00,
      0x80,
      0x00, // mov b6=r16; br b6;;
      // The lazy binding stub.
      0x11,
      0x78,
      0x00,
      0x00,
      0x00,
      0x24,
      0x00,
      0x00, // mov r15=index
      0x00,
      0x02,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x40, // nop.i 0; br header;;
  };
  memcpy(buf, entry, sizeof(entry));
  uint64_t gp = ctx.sym.ia64Gp->getVA(ctx);
  setImm22(buf, 0, sym.getGotPltVA(ctx) - gp);
  uint8_t *lazy = buf + importStubSize;
  setImm22(lazy, 0, sym.getPltIdx(ctx));
  uint64_t disp = ctx.in.plt->getVA() - (pltEntryAddr + importStubSize);
  setFields(lazy, 2, pcrel21bMask, pcrel21b(disp));
}

//===----------------------------------------------------------------------===//
// Applying relocations
//===----------------------------------------------------------------------===//

void IA64::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  // Instruction relocations are at the bundle address plus the slot number.
  unsigned slot = rel.offset & 3;
  uint8_t *bundle = loc - slot;

  switch (rel.type) {
  case R_IA64_NONE:
  case R_IA64_LDXMOV:
    break;

  case R_IA64_IMM14:
    checkInt(ctx, loc, val, 14, rel);
    setFields(bundle, slot, imm14Mask, imm14(val));
    break;
  case R_IA64_IMM22:
  case R_IA64_GPREL22:
  case R_IA64_LTOFF22:
  case R_IA64_LTOFF22X:
  case R_IA64_LTOFF_FPTR22:
    checkInt(ctx, loc, val, 22, rel);
    setImm22(bundle, slot, val);
    break;
  case R_IA64_IMM64:
  case R_IA64_GPREL64I:
  case R_IA64_LTOFF64I:
  case R_IA64_LTOFF_FPTR64I:
  case R_IA64_FPTR64I:
    setImm64(bundle, val);
    break;

  // The computed value is relative to the relocation offset, which includes
  // the slot number.
  case R_IA64_PCREL21B:
  case R_IA64_PCREL21BI:
    val += slot;
    checkInt(ctx, loc, val, 25, rel);
    checkAlignment(ctx, loc, val, 16, rel);
    setFields(bundle, slot, pcrel21bMask, pcrel21b(val));
    break;
  case R_IA64_PCREL21M:
    val += slot;
    checkInt(ctx, loc, val, 25, rel);
    checkAlignment(ctx, loc, val, 16, rel);
    setFields(bundle, slot, pcrel21mMask, pcrel21m(val));
    break;
  case R_IA64_PCREL21F:
    val += slot;
    checkInt(ctx, loc, val, 25, rel);
    checkAlignment(ctx, loc, val, 16, rel);
    setFields(bundle, slot, pcrel21fMask, pcrel21f(val));
    break;
  case R_IA64_PCREL60B:
    val += slot;
    checkAlignment(ctx, loc, val, 16, rel);
    setPcrel60(bundle, val);
    break;
  case R_IA64_PCREL22:
    val += slot;
    checkInt(ctx, loc, val, 22, rel);
    setImm22(bundle, slot, val);
    break;
  case R_IA64_PCREL64I:
    setImm64(bundle, val + slot);
    break;

  case R_IA64_DIR32LSB:
  case R_IA64_GPREL32LSB:
  case R_IA64_FPTR32LSB:
  case R_IA64_SEGREL32LSB:
  case R_IA64_SECREL32LSB:
  case R_IA64_LTV32LSB:
  case R_IA64_LTOFF_FPTR32LSB:
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_IA64_PCREL32LSB:
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_IA64_DIR64LSB:
  case R_IA64_GPREL64LSB:
  case R_IA64_FPTR64LSB:
  case R_IA64_PCREL64LSB:
  case R_IA64_SEGREL64LSB:
  case R_IA64_SECREL64LSB:
  case R_IA64_REL64LSB:
  case R_IA64_LTV64LSB:
  case R_IA64_LTOFF_FPTR64LSB:
    write64le(loc, val);
    break;

  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setIA64TargetInfo(Ctx &ctx) { ctx.target.reset(new IA64(ctx)); }
