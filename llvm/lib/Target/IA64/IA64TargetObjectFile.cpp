//===-- IA64TargetObjectFile.cpp - IA64 Object Info -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The linker places the short data sections next to the linkage table,
// where GP points, so an object in them is addressable with a single add of
// a 22-bit offset to GP. Objects of 8 bytes or less go there. All object
// files must agree on which objects those are, since the user of an object
// may not be the one that defines it.
//
//===----------------------------------------------------------------------===//

#include "IA64TargetObjectFile.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

void IA64TargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  // Unwind info is read-only, so it cannot hold addresses needing dynamic
  // relocations. The exception table references type info indirectly,
  // through a GP-relative offset to a word holding its address, which the
  // linker can resolve.
  TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_datarel |
                  dwarf::DW_EH_PE_udata8;

  ShortDataSection =
      Ctx.getELFSection(".sdata", ELF::SHT_PROGBITS,
                        ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_IA_64_SHORT);
  ShortBSSSection =
      Ctx.getELFSection(".sbss", ELF::SHT_NOBITS,
                        ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_IA_64_SHORT);
}

void IA64TargetObjectFile::getModuleMetadata(Module &M) {
  TargetLoweringObjectFileELF::getModuleMetadata(M);
  if (const auto *Limit = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("SmallDataLimit")))
    ShortDataLimit = Limit->getZExtValue();
}

const MCExpr *IA64TargetObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  if ((Encoding & 0x70) != dwarf::DW_EH_PE_datarel)
    return TargetLoweringObjectFileELF::getTTypeGlobalReference(
        GV, Encoding, TM, MMI, Streamer);

  const MCSymbol *Sym = TM.getSymbol(GV);
  if (Encoding & dwarf::DW_EH_PE_indirect) {
    MCSymbol *Stub = getSymbolWithGlobalValueBase(GV, ".DW.stub", TM);
    MachineModuleInfoImpl::StubValueTy &Entry =
        MMI->getObjFileInfo<MachineModuleInfoELF>().getGVStubEntry(Stub);
    if (!Entry.getPointer())
      Entry = MachineModuleInfoImpl::StubValueTy(TM.getSymbol(GV),
                                                 !GV->hasLocalLinkage());
    Sym = Stub;
  }
  return MCSpecifierExpr::create(MCSymbolRefExpr::create(Sym, getContext()),
                                 IA64::S_GPREL, getContext());
}

bool IA64TargetObjectFile::isShortData(const GlobalValue *GV,
                                       const TargetMachine &TM) const {
  const auto *Var = dyn_cast<GlobalVariable>(GV);
  if (!Var || Var->isThreadLocal())
    return false;

  // A section that the source names is the one the object is in, and a
  // common symbol is put with the others when the link editor makes room
  // for it.
  if (Var->hasSection() || Var->hasCommonLinkage())
    return false;

  Type *Ty = Var->getValueType();
  if (!Ty->isSized())
    return false;
  uint64_t Size = Var->getDataLayout().getTypeAllocSize(Ty);
  if (Size == 0 || Size > ShortDataLimit)
    return false;

  // A declared object's definition may be in another module, unless its
  // visibility says otherwise.
  if (Var->isDeclaration())
    return !Var->hasDefaultVisibility();

  // A local constant is better placed in a mergeable section with the
  // constants equal to it.
  if (Var->hasLocalLinkage()) {
    SectionKind Kind = getKindForGlobal(Var, TM);
    if (Kind.isMergeableConst() || Kind.isMergeableCString())
      return false;
  }
  return true;
}

// Read-only short data still goes in .sdata, since the module's read-only
// data is not where GP points.
MCSection *IA64TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (!isShortData(GO, TM))
    return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);

  // An object in a comdat group, kept or discarded with it, gets its own
  // section in the group.
  bool IsBSS = Kind.isBSS();
  const Comdat *Group = GO->getComdat();
  if (!TM.getDataSections() && !Group)
    return IsBSS ? ShortBSSSection : ShortDataSection;

  SmallString<128> Name(IsBSS ? ".sbss." : ".sdata.");
  Name += TM.getSymbol(GO)->getName();
  unsigned Flags = ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_IA_64_SHORT;
  unsigned Type = IsBSS ? ELF::SHT_NOBITS : ELF::SHT_PROGBITS;
  if (!Group)
    return getContext().getELFSection(Name, Type, Flags);
  return getContext().getELFSection(
      Name, Type, Flags | ELF::SHF_GROUP, 0, Group->getName(),
      Group->getSelectionKind() != Comdat::NoDeduplicate);
}
