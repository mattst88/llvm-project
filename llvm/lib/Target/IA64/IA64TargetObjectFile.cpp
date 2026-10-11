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
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

void IA64TargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  ShortDataSection =
      Ctx.getELFSection(".sdata", ELF::SHT_PROGBITS,
                        ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_IA_64_SHORT);
  ShortBSSSection =
      Ctx.getELFSection(".sbss", ELF::SHT_NOBITS,
                        ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_IA_64_SHORT);
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
