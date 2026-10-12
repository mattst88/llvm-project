//===-- IA64RegisterBankInfo.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the RegisterBankInfo class for IA64.
//
//===----------------------------------------------------------------------===//

#include "IA64RegisterBankInfo.h"
#include "IA64RegisterInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_TARGET_REGBANK_IMPL
#include "IA64GenRegisterBank.inc"

using namespace llvm;

// A GR is 64 bits, and narrower values live in its low bits. An FR holds
// float, double and f80 alike, so one mapping as wide as the widest serves
// all three.
static const RegisterBankInfo::PartialMapping PartMappings[] = {
    {0, 64, IA64::GPRRegBank},
    {0, 80, IA64::FPRRegBank},
};

static const RegisterBankInfo::ValueMapping GPRMapping = {&PartMappings[0], 1};
static const RegisterBankInfo::ValueMapping FPRMapping = {&PartMappings[1], 1};

// Operations whose register operands are all FP. Compares and conversions
// are excluded: one side of each is an integer.
static bool isFPOpcode(unsigned Opc) {
  switch (Opc) {
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FDIV:
  case TargetOpcode::G_FMA:
  case TargetOpcode::G_FNEG:
  case TargetOpcode::G_FABS:
  case TargetOpcode::G_FCOPYSIGN:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FCONSTANT:
  case TargetOpcode::G_FPEXT:
  case TargetOpcode::G_FPTRUNC:
    return true;
  default:
    return false;
  }
}

static bool isFPRegister(Register Reg, const MachineRegisterInfo &MRI) {
  if (Reg.isPhysical())
    return IA64::FRRegClass.contains(Reg);
  if (const RegisterBank *RB = MRI.getRegBankOrNull(Reg))
    return RB->getID() == IA64::FPRRegBankID;
  if (const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg))
    return IA64::FRRegClass.hasSubClassEq(RC);
  // Only an FR is wide enough for an f80.
  LLT Ty = MRI.getType(Reg);
  return Ty.isValid() && Ty.getSizeInBits() > 64;
}

// Whether the definition of Reg leaves it in an FR. Loads, phis and selects
// do not determine the value's kind, and take the bank of the surrounding
// values.
static bool definedByFP(Register Reg, const MachineRegisterInfo &MRI,
                        SmallPtrSetImpl<const MachineInstr *> &Seen) {
  if (isFPRegister(Reg, MRI))
    return true;
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (!Def)
    return false;
  if (isFPOpcode(Def->getOpcode()))
    return true;
  switch (Def->getOpcode()) {
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP:
    return true;
  // An incoming argument is a copy from its physical register.
  case TargetOpcode::COPY:
    return Def->getOperand(1).getReg().isPhysical() &&
           isFPRegister(Def->getOperand(1).getReg(), MRI);
  // A phi is FP if any incoming value is. A loop phi may reach itself, so
  // each phi is visited once.
  case TargetOpcode::G_PHI:
    if (!Seen.insert(Def).second)
      return false;
    for (unsigned I = 1, E = Def->getNumOperands(); I < E; I += 2)
      if (definedByFP(Def->getOperand(I).getReg(), MRI, Seen))
        return true;
    return false;
  case TargetOpcode::G_SELECT:
    if (!Seen.insert(Def).second)
      return false;
    return definedByFP(Def->getOperand(2).getReg(), MRI, Seen) ||
           definedByFP(Def->getOperand(3).getReg(), MRI, Seen);
  case TargetOpcode::G_FREEZE:
  case TargetOpcode::G_CONSTANT_FOLD_BARRIER:
    return definedByFP(Def->getOperand(1).getReg(), MRI, Seen);
  default:
    return false;
  }
}

static bool definedByFP(Register Reg, const MachineRegisterInfo &MRI) {
  SmallPtrSet<const MachineInstr *, 8> Seen;
  return definedByFP(Reg, MRI, Seen);
}

// Whether a user of Reg wants it in an FR. A use through a phi or select
// counts as a use of the phi or select result, which is FP if either its
// uses or its definition are.
static bool usedByFP(Register Reg, const MachineRegisterInfo &MRI,
                     SmallPtrSetImpl<const MachineInstr *> &Seen) {
  if (isFPRegister(Reg, MRI))
    return true;
  for (const MachineInstr &Use : MRI.use_nodbg_instructions(Reg)) {
    if (isFPOpcode(Use.getOpcode()))
      return true;
    switch (Use.getOpcode()) {
    case TargetOpcode::G_FCMP:
    case TargetOpcode::G_FPTOSI:
    case TargetOpcode::G_FPTOUI:
      return true;
    // A returned or passed value is copied to its physical register.
    case TargetOpcode::COPY:
      if (Use.getOperand(0).getReg().isPhysical() &&
          isFPRegister(Use.getOperand(0).getReg(), MRI))
        return true;
      break;
    case TargetOpcode::G_SELECT:
      if (Use.getOperand(1).getReg() == Reg)
        break;
      [[fallthrough]];
    case TargetOpcode::G_PHI:
    case TargetOpcode::G_FREEZE:
    case TargetOpcode::G_CONSTANT_FOLD_BARRIER: {
      if (!Seen.insert(&Use).second)
        break;
      Register Def = Use.getOperand(0).getReg();
      SmallPtrSet<const MachineInstr *, 8> SeenDefs;
      if (usedByFP(Def, MRI, Seen) || definedByFP(Def, MRI, SeenDefs))
        return true;
      break;
    }
    default:
      break;
    }
  }
  return false;
}

static bool usedByFP(Register Reg, const MachineRegisterInfo &MRI) {
  SmallPtrSet<const MachineInstr *, 8> Seen;
  return usedByFP(Reg, MRI, Seen);
}

IA64RegisterBankInfo::IA64RegisterBankInfo() = default;

const RegisterBankInfo::InstructionMapping &
IA64RegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  // A vreg in neither bank is a BR or predicate requested by inline asm, and
  // moving values into those is not supported here.
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    const TargetRegisterClass *RC = MRI.getRegClassOrNull(MO.getReg());
    if (RC && !IA64::GRRegClass.hasSubClassEq(RC) &&
        !IA64::GRSaveRegClass.hasSubClassEq(RC) &&
        !IA64::FRRegClass.hasSubClassEq(RC))
      return getInvalidInstructionMapping();
  }

  // A copy to or from a BR moves an integer, and the BR itself has no bank.
  if (MI.isCopy()) {
    for (const MachineOperand &MO : MI.operands())
      if (MO.getReg().isPhysical() && IA64::BRRegClass.contains(MO.getReg()))
        return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                                     getOperandsMapping({&GPRMapping}),
                                     /*NumOperands=*/1);
  }

  if (!isPreISelGenericOpcode(Opc)) {
    const InstructionMapping &Mapping = getInstrMappingImpl(MI);
    if (Mapping.isValid())
      return Mapping;
  }

  using namespace TargetOpcode;

  unsigned NumOperands = MI.getNumOperands();

  // The bank of each register operand, GPR unless overridden below.
  // Non-register operands have no mapping.
  SmallVector<const ValueMapping *, 8> Ops(NumOperands, nullptr);
  auto SetAll = [&](const ValueMapping &Mapping) {
    for (unsigned I = 0; I != NumOperands; ++I)
      if (MI.getOperand(I).isReg() && MI.getOperand(I).getReg())
        Ops[I] = &Mapping;
  };
  auto Bank = [](bool IsFP) { return IsFP ? &FPRMapping : &GPRMapping; };
  SetAll(GPRMapping);

  // Debug instructions reference registers but generate no code. A vreg with
  // no bank yet gets the one assigned here, so use the bank of its
  // definition.
  if (MI.isDebugInstr()) {
    for (unsigned I = 0; I != NumOperands; ++I) {
      const MachineOperand &MO = MI.getOperand(I);
      if (!MO.isReg() || !MO.getReg().isVirtual()) {
        Ops[I] = nullptr;
        continue;
      }
      // A value the legalizer split is still referenced whole here, and no
      // single register holds it. Map it as the GPRs that would hold its
      // 64-bit words, without creating any: no repair code is inserted for a
      // debug instruction.
      LLT Ty = MRI.getType(MO.getReg());
      if (Ty.isValid() && Ty.getSizeInBits() > 80) {
        unsigned Size = Ty.getSizeInBits();
        unsigned NumWords = divideCeil(Size, 64);
        std::unique_ptr<PartialMapping[]> &Words = WordsOf[Size];
        if (!Words) {
          Words = std::make_unique<PartialMapping[]>(NumWords);
          for (unsigned W = 0; W != NumWords; ++W)
            Words[W] = PartialMapping(W * 64, std::min(64u, Size - W * 64),
                                      IA64::GPRRegBank);
        }
        Ops[I] = &getValueMapping(Words.get(), NumWords);
        continue;
      }
      Ops[I] = Bank(definedByFP(MO.getReg(), MRI));
    }
    return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                                 getOperandsMapping(Ops), NumOperands);
  }

  switch (Opc) {
  // Integer operations, with all register operands in GPRs.
  case G_ADD:
  case G_SUB:
  case G_MUL:
  case G_UMULH:
  case G_SMULH:
  case G_AND:
  case G_OR:
  case G_XOR:
  case G_SHL:
  case G_LSHR:
  case G_ASHR:
  case G_PTR_ADD:
  case G_PTRMASK:
  case G_ICMP:
  case G_CONSTANT:
  case G_FRAME_INDEX:
  case G_GLOBAL_VALUE:
  case G_JUMP_TABLE:
  case G_BLOCK_ADDR:
  case G_CONSTANT_POOL:
  case G_INTTOPTR:
  case G_PTRTOINT:
  case G_ZEXT:
  case G_SEXT:
  case G_ANYEXT:
  case G_TRUNC:
  case G_SEXT_INREG:
  case G_CTPOP:
  case G_BSWAP:
  case G_BR:
  case G_BRCOND:
  case G_BRINDIRECT:
  case G_BRJT:
  case G_ATOMIC_CMPXCHG:
  case G_ATOMICRMW_XCHG:
  case G_ATOMICRMW_ADD:
  case G_DYN_STACKALLOC:
  case G_STACKSAVE:
  case G_STACKRESTORE:
  case G_VASTART:
  case G_PREFETCH:
  case G_TRAP:
  case G_DEBUGTRAP:
  case G_INVOKE_REGION_START:
  case G_FENCE:
  // Extending loads read a field narrower than the register, which no FP
  // load does.
  case G_ZEXTLOAD:
    break;
  case G_FADD:
  case G_FSUB:
  case G_FMUL:
  case G_FDIV:
  case G_FMA:
  case G_FNEG:
  case G_FABS:
  case G_FCOPYSIGN:
  case G_FMINNUM:
  case G_FMAXNUM:
  case G_FCONSTANT:
  case G_FPEXT:
  case G_FPTRUNC:
    SetAll(FPRMapping);
    break;
  // The compare result is an integer.
  case G_FCMP:
    Ops[2] = Ops[3] = &FPRMapping;
    break;
  // The integer source of an int-to-FP conversion is in an FR, and the
  // selector moves it there; likewise the other way.
  case G_SITOFP:
  case G_UITOFP:
    Ops[0] = &FPRMapping;
    break;
  case G_FPTOSI:
  case G_FPTOUI:
    Ops[1] = &FPRMapping;
    break;
  case G_LOAD:
  case G_STORE: {
    // A load or store narrower than the value is an integer access: stfs and
    // stfd write a whole float or double, and an f80's 16 bytes are the
    // whole value too.
    Register ValReg = MI.getOperand(0).getReg();
    LLT Ty = MRI.getType(ValReg);
    bool FullWidth =
        cast<GLoadStore>(MI).getMemSizeInBits() == Ty.getSizeInBits() &&
        Ty.isScalar() && Ty.getSizeInBits() >= 32;
    bool IsFP = FullWidth && (Opc == G_LOAD ? usedByFP(ValReg, MRI)
                                            : definedByFP(ValReg, MRI));
    Ops[0] = Bank(IsFP);
    break;
  }
  case G_SELECT: {
    // The condition is an integer; the operands and the result share one
    // bank, FPR if either the uses or the definitions are FP.
    bool IsFP = usedByFP(MI.getOperand(0).getReg(), MRI) ||
                definedByFP(MI.getOperand(0).getReg(), MRI);
    Ops[0] = Ops[2] = Ops[3] = Bank(IsFP);
    break;
  }
  case G_IMPLICIT_DEF:
    Ops[0] = Bank(usedByFP(MI.getOperand(0).getReg(), MRI));
    break;
  case G_FREEZE:
  case G_CONSTANT_FOLD_BARRIER: {
    bool IsFP = usedByFP(MI.getOperand(0).getReg(), MRI) ||
                definedByFP(MI.getOperand(1).getReg(), MRI);
    Ops[0] = Ops[1] = Bank(IsFP);
    break;
  }
  case G_PHI: {
    // The incoming values and the result share one bank, as the single
    // mapping of a copy-like instruction implies. It is FPR if either the
    // uses or the definitions are FP: once the banks differ, the two kinds
    // of copy cannot be told apart.
    bool IsFP = usedByFP(MI.getOperand(0).getReg(), MRI) ||
                definedByFP(MI.getOperand(0).getReg(), MRI);
    return getInstructionMapping(DefaultMappingID, /*Cost=*/1, Bank(IsFP),
                                 /*NumOperands=*/1);
  }
  default:
    return getInvalidInstructionMapping();
  }

  return getInstructionMapping(DefaultMappingID, /*Cost=*/1,
                               getOperandsMapping(Ops), NumOperands);
}
