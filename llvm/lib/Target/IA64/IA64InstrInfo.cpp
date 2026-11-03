//===-- IA64InstrInfo.cpp - IA64 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstrInfo.h"
#include "IA64.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "IA64GenInstrInfo.inc"

using namespace llvm;

IA64InstrInfo::IA64InstrInfo(const IA64Subtarget &STI)
    : IA64GenInstrInfo(STI, RI, IA64::ADJCALLSTACKDOWN, IA64::ADJCALLSTACKUP),
      RI() {}

void IA64InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  // GR copy: adds r1 = 0, r2, which is what the "mov" pseudo-op assembles
  // to.
  if (IA64::GRRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::ADDS), DestReg)
        .addImm(0)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(IA64::P0);
    return;
  }
  // FR copy: fmerge.s f1 = f2, f2, which is what the "mov" pseudo-op
  // assembles to.
  if (IA64::FRRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::FMERGE_S), DestReg)
        .addReg(SrcReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(IA64::P0);
    return;
  }
  if (IA64::GRRegClass.contains(DestReg) && IA64::BRRegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::MOV_FROM_BR), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(IA64::P0);
    return;
  }
  if (IA64::BRRegClass.contains(DestReg) && IA64::GRRegClass.contains(SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::MOV_BR), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addImm(0)
        .addReg(IA64::P0);
    return;
  }
  // PR copy: (src) cmp.eq.unc dst, p0 = r0, r0, which clears dst when src
  // is clear.
  if (IA64::PRValueRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::CMP_EQ_UNC))
        .addReg(DestReg, RegState::Define)
        .addReg(IA64::P0, RegState::Define)
        .addReg(IA64::R0)
        .addReg(IA64::R0)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  report_fatal_error("IA64 cannot copy between these registers yet");
}

// Spills and reloads are single pseudos, expanded into the address add and
// the store or load once the frame is laid out. If they were two
// instructions from the start, a register allocator spilling the result of a
// reload could put its store between them.
void IA64InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  unsigned Opc;
  if (IA64::GRRegClass.hasSubClassEq(RC))
    Opc = IA64::SPILL_GR;
  else if (IA64::FRRegClass.hasSubClassEq(RC))
    Opc = IA64::SPILL_FR;
  else if (IA64::PRValueRegClass.hasSubClassEq(RC))
    Opc = IA64::SPILL_PR;
  else
    report_fatal_error("IA64 cannot spill a register of this class yet");
  BuildMI(MBB, MI, DebugLoc(), get(Opc))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .setMIFlag(Flags);
}

void IA64InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  unsigned Opc;
  if (IA64::GRRegClass.hasSubClassEq(RC))
    Opc = IA64::RELOAD_GR;
  else if (IA64::FRRegClass.hasSubClassEq(RC))
    Opc = IA64::RELOAD_FR;
  else if (IA64::PRValueRegClass.hasSubClassEq(RC))
    Opc = IA64::RELOAD_PR;
  else
    report_fatal_error("IA64 cannot reload a register of this class yet");
  BuildMI(MBB, MI, DebugLoc(), get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .setMIFlag(Flags);
}

Register IA64InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case IA64::RELOAD_GR:
  case IA64::RELOAD_FR:
  case IA64::RELOAD_PR:
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  default:
    return Register();
  }
}

Register IA64InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case IA64::SPILL_GR:
  case IA64::SPILL_FR:
  case IA64::SPILL_PR:
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  default:
    return Register();
  }
}

void IA64InstrInfo::loadImmediate(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  const DebugLoc &DL, Register Reg, int64_t Imm,
                                  MachineInstr::MIFlag Flags) const {
  if (isInt<22>(Imm))
    BuildMI(MBB, MI, DL, get(IA64::ADDL), Reg)
        .addImm(Imm)
        .addReg(IA64::R0)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
  else
    BuildMI(MBB, MI, DL, get(IA64::MOVL), Reg)
        .addImm(Imm)
        .addReg(IA64::P0)
        .setMIFlag(Flags);
}

// The encoded compares are EQ and LT, signed and unsigned, with any
// immediate as the left operand. The other conditions swap the operands,
// swap the two result predicates, or compare against the constant minus
// one: r < c is !(c - 1 < r). cmp4 has the same forms as cmp.
bool IA64CC::isLegalImmediate(CondCode CC, int64_t Imm) {
  switch (getDoubleword(CC)) {
  case LT:
  case GE:
    return isInt<8>(Imm - 1);
  case LTU:
  case GEU:
    return Imm != 0 && isInt<8>(Imm - 1);
  case BitClear:
  case BitSet:
    return isUInt<6>(Imm);
  case NotNaT:
  case NaT:
    return Imm == 0;
  default:
    return isInt<8>(Imm);
  }
}

// The cmp4 form of a cmp, or 0 for 0.
static unsigned getWordCompare(unsigned Opcode) {
  switch (Opcode) {
  case 0:
    return 0;
  case IA64::CMP_EQ:
    return IA64::CMP4_EQ;
  case IA64::CMP_EQi:
    return IA64::CMP4_EQi;
  case IA64::CMP_LT:
    return IA64::CMP4_LT;
  case IA64::CMP_LTi:
    return IA64::CMP4_LTi;
  case IA64::CMP_LTU:
    return IA64::CMP4_LTU;
  case IA64::CMP_LTUi:
    return IA64::CMP4_LTUi;
  case IA64::CMP_EQ_OR_ANDCM:
    return IA64::CMP4_EQ_OR_ANDCM;
  case IA64::CMP_EQ_OR_ANDCMi:
    return IA64::CMP4_EQ_OR_ANDCMi;
  case IA64::CMP_NE_OR_ANDCM:
    return IA64::CMP4_NE_OR_ANDCM;
  case IA64::CMP_NE_OR_ANDCMi:
    return IA64::CMP4_NE_OR_ANDCMi;
  case IA64::CMP_GT_OR_ANDCMz:
    return IA64::CMP4_GT_OR_ANDCMz;
  case IA64::CMP_GE_OR_ANDCMz:
    return IA64::CMP4_GE_OR_ANDCMz;
  case IA64::CMP_LT_OR_ANDCMz:
    return IA64::CMP4_LT_OR_ANDCMz;
  case IA64::CMP_LE_OR_ANDCMz:
    return IA64::CMP4_LE_OR_ANDCMz;
  default:
    llvm_unreachable("Not a compare with a cmp4 form");
  }
}

void IA64InstrInfo::emitCompare(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, IA64CC::CondCode CC,
                                const MachineOperand &LHS,
                                const MachineOperand &RHS,
                                CompareKind Kind) const {
  // Copy only the register and its undef flag. Kill flags are dropped, since
  // an operand may be read more than once here.
  auto Read = [](const MachineOperand &MO) {
    return MachineOperand::CreateReg(MO.getReg(), /*isDef=*/false,
                                     /*isImp=*/false, /*isKill=*/false,
                                     /*isDead=*/false, MO.isUndef());
  };
  MachineOperand L = Read(LHS);
  if (IA64CC::isFloatingPoint(CC)) {
    // The encoded conditions are eq, lt, le and unord; all but unord are
    // false for unordered operands. The others swap the operands or the
    // result predicates.
    unsigned Opcode;
    bool Invert = (CC - IA64CC::OEQ) & 1, Swap = false;
    switch (CC) {
    case IA64CC::OEQ:
    case IA64CC::UNE:
      Opcode = IA64::FCMP_EQ;
      break;
    case IA64CC::OGT:
    case IA64CC::ULE:
      Swap = true;
      [[fallthrough]];
    case IA64CC::OLT:
    case IA64CC::UGE:
      Opcode = IA64::FCMP_LT;
      break;
    case IA64CC::OGE:
    case IA64CC::ULT:
      Swap = true;
      [[fallthrough]];
    case IA64CC::OLE:
    case IA64CC::UGT:
      Opcode = IA64::FCMP_LE;
      break;
    case IA64CC::UO:
    case IA64CC::ORD:
      Opcode = IA64::FCMP_UNORD;
      break;
    default:
      llvm_unreachable("Not the condition of a floating-point compare");
    }
    // The predicates set when the encoded condition is true and false.
    Register True = Invert ? IA64::P7 : IA64::P6;
    Register False = Invert ? IA64::P6 : IA64::P7;
    Register Qualifier = IA64::P0;
    bool Signals = Opcode == IA64::FCMP_LT || Opcode == IA64::FCMP_LE;
    if (Kind == QuietCompare && Signals) {
      // Test unord first, and do the ordered compare only if the operands
      // are ordered. Otherwise the predicates are left saying the condition
      // is false.
      BuildMI(MBB, I, DL, get(IA64::FCMP_UNORD))
          .addReg(False, RegState::Define)
          .addReg(True, RegState::Define)
          .add(L)
          .add(Read(RHS))
          .addImm(0)
          .addReg(IA64::P0);
      Qualifier = True;
    } else if (Kind == SignalingCompare && !Signals) {
      // A compare only for its exception; the next compare overwrites its
      // result.
      BuildMI(MBB, I, DL, get(IA64::FCMP_LT))
          .addReg(True, RegState::Define)
          .addReg(False, RegState::Define)
          .add(L)
          .add(Read(RHS))
          .addImm(0)
          .addReg(IA64::P0);
    }
    BuildMI(MBB, I, DL, get(Opcode))
        .addReg(True, RegState::Define)
        .addReg(False, RegState::Define)
        .add(Swap ? Read(RHS) : L)
        .add(Swap ? L : Read(RHS))
        .addImm(0)
        .addReg(Qualifier);
    return;
  }

  if (IA64CC::isBitTest(CC)) {
    // tbit.nz is tbit.z with its predicates swapped.
    bool Set = CC == IA64CC::BitSet;
    BuildMI(MBB, I, DL, get(IA64::TBIT_Z))
        .addReg(Set ? IA64::P7 : IA64::P6, RegState::Define)
        .addReg(Set ? IA64::P6 : IA64::P7, RegState::Define)
        .add(L)
        .addImm(RHS.getImm())
        .addReg(IA64::P0);
    return;
  }

  if (IA64CC::isNaTTest(CC)) {
    bool Set = CC == IA64CC::NaT;
    BuildMI(MBB, I, DL, get(IA64::TNAT_Z))
        .addReg(Set ? IA64::P7 : IA64::P6, RegState::Define)
        .addReg(Set ? IA64::P6 : IA64::P7, RegState::Define)
        .add(L)
        .addReg(IA64::P0);
    return;
  }

  bool Word = IA64CC::isWord(CC);
  CC = IA64CC::getDoubleword(CC);
  bool IsImm = RHS.isImm();
  unsigned Opcode;
  // Whether the encoded compare is the inverse of the requested one, and
  // whether the requested LHS register becomes its right operand.
  bool Invert = false, Swap = false;
  int64_t Adjust = 0;
  switch (CC) {
  case IA64CC::NE:
    Invert = true;
    [[fallthrough]];
  case IA64CC::EQ:
    Opcode = IsImm ? IA64::CMP_EQi : IA64::CMP_EQ;
    break;
  case IA64CC::GE:
  case IA64CC::GEU:
    Invert = true;
    [[fallthrough]];
  case IA64CC::LT:
  case IA64CC::LTU:
    // With an immediate: r < c is !(c - 1 < r).
    Invert ^= IsImm;
    Adjust = -1;
    break;
  case IA64CC::LE:
  case IA64CC::LEU:
    Invert = true;
    [[fallthrough]];
  case IA64CC::GT:
  case IA64CC::GTU:
    // r > s is s < r, and an immediate is already on the left.
    Swap = !IsImm;
    break;
  default:
    llvm_unreachable("Not the condition of an integer compare");
  }
  switch (CC) {
  case IA64CC::EQ:
  case IA64CC::NE:
    break;
  case IA64CC::LT:
  case IA64CC::GE:
  case IA64CC::GT:
  case IA64CC::LE:
    Opcode = IsImm ? IA64::CMP_LTi : IA64::CMP_LT;
    break;
  case IA64CC::LTU:
  case IA64CC::GEU:
  case IA64CC::GTU:
  case IA64CC::LEU:
    Opcode = IsImm ? IA64::CMP_LTUi : IA64::CMP_LTU;
    break;
  default:
    llvm_unreachable("Not the condition of an integer compare");
  }
  if (Word)
    Opcode = getWordCompare(Opcode);

  MachineInstrBuilder MIB =
      BuildMI(MBB, I, DL, get(Opcode))
          .addReg(Invert ? IA64::P7 : IA64::P6, RegState::Define)
          .addReg(Invert ? IA64::P6 : IA64::P7, RegState::Define);
  if (IsImm) {
    assert(IA64CC::isLegalImmediate(CC, RHS.getImm()) &&
           "No compare takes this constant");
    MIB.addImm(RHS.getImm() + Adjust).add(L);
  } else if (Swap) {
    MIB.add(Read(RHS)).add(L);
  } else {
    MIB.add(L).add(Read(RHS));
  }
  MIB.addReg(IA64::P0);
}

unsigned IA64CC::getParallelCompare(CondCode CC, std::optional<int64_t> Imm) {
  if (isWord(CC))
    return getWordCompare(getParallelCompare(getDoubleword(CC), Imm));
  if (CC == EQ || CC == NE) {
    if (Imm && !isInt<8>(*Imm))
      return 0;
    if (CC == EQ)
      return Imm ? IA64::CMP_EQ_OR_ANDCMi : IA64::CMP_EQ_OR_ANDCM;
    return Imm ? IA64::CMP_NE_OR_ANDCMi : IA64::CMP_NE_OR_ANDCM;
  }
  if (!Imm)
    return 0;
  if (CC == BitClear)
    return IA64::TBIT_Z_OR_ANDCM;
  if (CC == BitSet)
    return IA64::TBIT_NZ_OR_ANDCM;
  // The instruction has zero on the left: "r < 0" is "0 > r".
  switch (CC) {
  case LT:
    return *Imm == 0   ? IA64::CMP_GT_OR_ANDCMz
           : *Imm == 1 ? IA64::CMP_GE_OR_ANDCMz
                       : 0;
  case LE:
    return *Imm == 0    ? IA64::CMP_GE_OR_ANDCMz
           : *Imm == -1 ? IA64::CMP_GT_OR_ANDCMz
                        : 0;
  case GT:
    return *Imm == 0    ? IA64::CMP_LT_OR_ANDCMz
           : *Imm == -1 ? IA64::CMP_LE_OR_ANDCMz
                        : 0;
  case GE:
    return *Imm == 0   ? IA64::CMP_LE_OR_ANDCMz
           : *Imm == 1 ? IA64::CMP_LT_OR_ANDCMz
                       : 0;
  default:
    return 0;
  }
}

unsigned IA64CC::getAndCompare(CondCode CC, std::optional<int64_t> Imm) {
  if (isNaTTest(CC))
    return CC == NotNaT ? IA64::TNAT_Z_AND : IA64::TNAT_NZ_AND;
  switch (getParallelCompare(CC, Imm)) {
  case IA64::CMP_EQ_OR_ANDCM:
    return IA64::CMP_EQ_AND;
  case IA64::CMP_EQ_OR_ANDCMi:
    return IA64::CMP_EQ_ANDi;
  case IA64::CMP_NE_OR_ANDCM:
    return IA64::CMP_NE_AND;
  case IA64::CMP_NE_OR_ANDCMi:
    return IA64::CMP_NE_ANDi;
  case IA64::CMP_GT_OR_ANDCMz:
    return IA64::CMP_GT_ANDz;
  case IA64::CMP_GE_OR_ANDCMz:
    return IA64::CMP_GE_ANDz;
  case IA64::CMP_LT_OR_ANDCMz:
    return IA64::CMP_LT_ANDz;
  case IA64::CMP_LE_OR_ANDCMz:
    return IA64::CMP_LE_ANDz;
  case IA64::CMP4_EQ_OR_ANDCM:
    return IA64::CMP4_EQ_AND;
  case IA64::CMP4_EQ_OR_ANDCMi:
    return IA64::CMP4_EQ_ANDi;
  case IA64::CMP4_NE_OR_ANDCM:
    return IA64::CMP4_NE_AND;
  case IA64::CMP4_NE_OR_ANDCMi:
    return IA64::CMP4_NE_ANDi;
  case IA64::CMP4_GT_OR_ANDCMz:
    return IA64::CMP4_GT_ANDz;
  case IA64::CMP4_GE_OR_ANDCMz:
    return IA64::CMP4_GE_ANDz;
  case IA64::CMP4_LT_OR_ANDCMz:
    return IA64::CMP4_LT_ANDz;
  case IA64::CMP4_LE_OR_ANDCMz:
    return IA64::CMP4_LE_ANDz;
  case IA64::TBIT_Z_OR_ANDCM:
    return IA64::TBIT_Z_AND;
  case IA64::TBIT_NZ_OR_ANDCM:
    return IA64::TBIT_NZ_AND;
  default:
    return 0;
  }
}

// The operands of a parallel or and-type compare after its predicates: an
// equality compare's immediate comes first, a bit test's last, and the
// compares against zero and the NaT tests have only the register.
static void addCompareOperands(MachineInstrBuilder &MIB, IA64CC::CondCode CC,
                               const MachineOperand &LHS,
                               const MachineOperand &RHS) {
  // As in emitCompare, keep only the register and its undef flag.
  MachineOperand L = MachineOperand::CreateReg(
      LHS.getReg(), /*isDef=*/false, /*isImp=*/false, /*isKill=*/false,
      /*isDead=*/false, LHS.isUndef());
  CC = IA64CC::getDoubleword(CC);
  if (CC == IA64CC::EQ || CC == IA64CC::NE) {
    if (RHS.isImm())
      MIB.addImm(RHS.getImm()).add(L);
    else
      MIB.add(L).addReg(RHS.getReg(), getUndefRegState(RHS.isUndef()));
  } else if (IA64CC::isBitTest(CC)) {
    MIB.add(L).addImm(RHS.getImm());
  } else {
    MIB.add(L);
  }
  MIB.addReg(IA64::P0);
}

bool IA64InstrInfo::emitAndCompare(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, IA64CC::CondCode CC,
                                   const MachineOperand &LHS,
                                   const MachineOperand &RHS,
                                   Register Pred) const {
  std::optional<int64_t> Imm;
  if (RHS.isImm())
    Imm = RHS.getImm();
  unsigned Opcode = IA64CC::getAndCompare(CC, Imm);
  if (!Opcode)
    return false;
  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opcode))
                                .addReg(Pred, RegState::Define)
                                .addReg(IA64::P0, RegState::Define);
  addCompareOperands(MIB, CC, LHS, RHS);
  return true;
}

bool IA64InstrInfo::emitParallelCompare(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        const DebugLoc &DL, IA64CC::CondCode CC,
                                        const MachineOperand &LHS,
                                        const MachineOperand &RHS, Register Set,
                                        Register Clear) const {
  std::optional<int64_t> Imm;
  if (RHS.isImm())
    Imm = RHS.getImm();
  unsigned Opcode = IA64CC::getParallelCompare(CC, Imm);
  if (!Opcode)
    return false;
  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opcode))
                                .addReg(Set, RegState::Define)
                                .addReg(Clear, RegState::Define);
  addCompareOperands(MIB, CC, LHS, RHS);
  return true;
}

// Whether a pseudo combines compares by AND or by OR, if it is one that
// combines them.
static std::optional<bool> isAndChain(unsigned Opcode) {
  switch (Opcode) {
  case IA64::SETCC_AND:
  case IA64::SETCC_ANDf:
  case IA64::SELECT_AND:
  case IA64::SELECT_ANDf:
  case IA64::SELECT_FR_AND:
  case IA64::SELECT_FR_ANDf:
    return true;
  case IA64::SETCC_OR:
  case IA64::SETCC_ORf:
  case IA64::SELECT_OR:
  case IA64::SELECT_ORf:
  case IA64::SELECT_FR_OR:
  case IA64::SELECT_FR_ORf:
    return false;
  default:
    return std::nullopt;
  }
}

// For an AND, each compare clears p6 and sets p7 if its condition is false;
// for an OR, it sets p6 and clears p7 if it is true. They write the
// predicates only one way, so they need no stops between them. A condition
// with no parallel compare is an ordinary compare instead, under p6 for an
// AND and p7 for an OR, so it decides only when the others have not.
void IA64InstrInfo::emitChainCompares(MachineBasicBlock &MBB, MachineInstr &MI,
                                      const DebugLoc &DL, unsigned First,
                                      bool And) const {
  for (unsigned I = First, E = MI.getNumExplicitOperands(); I < E; I += 3) {
    auto CC = IA64CC::CondCode(MI.getOperand(I).getImm());
    if (emitParallelCompare(MBB, MI, DL, And ? IA64CC::getOpposite(CC) : CC,
                            MI.getOperand(I + 1), MI.getOperand(I + 2),
                            And ? IA64::P7 : IA64::P6,
                            And ? IA64::P6 : IA64::P7))
      continue;
    assert(!IA64CC::isFloatingPoint(CC) && !IA64CC::isNaTTest(CC) &&
           "Only an integer compare may be predicated in a chain");
    emitCompare(MBB, MI, DL, CC, MI.getOperand(I + 1), MI.getOperand(I + 2));
    MachineInstr &Cmp = *std::prev(MachineBasicBlock::iterator(MI));
    Cmp.getOperand(Cmp.getNumExplicitOperands() - 1)
        .setReg(And ? IA64::P6 : IA64::P7);
    Cmp.addOperand(MachineOperand::CreateReg(And ? IA64::P7 : IA64::P6,
                                             /*isDef=*/false,
                                             /*isImp=*/true));
  }
}

ArrayRef<std::pair<unsigned, const char *>>
IA64InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  static const std::pair<unsigned, const char *> Flags[] = {
      {IA64::S_GPREL, "ia64-gprel"},
      {IA64::S_LTOFF, "ia64-ltoff"},
      {IA64::S_LTOFFX, "ia64-ltoffx"},
      {IA64::S_PLTOFF, "ia64-pltoff"},
      {IA64::S_PCREL, "ia64-pcrel"},
      {IA64::S_FPTR, "ia64-fptr"},
      {IA64::S_SECREL, "ia64-secrel"},
      {IA64::S_SEGREL, "ia64-segrel"},
      {IA64::S_LTV, "ia64-ltv"},
      {IA64::S_TPREL, "ia64-tprel"},
      {IA64::S_DTPMOD, "ia64-dtpmod"},
      {IA64::S_DTPREL, "ia64-dtprel"},
      {IA64::S_LTOFF_FPTR, "ia64-ltoff-fptr"},
      {IA64::S_LTOFF_TPREL, "ia64-ltoff-tprel"},
      {IA64::S_LTOFF_DTPMOD, "ia64-ltoff-dtpmod"},
      {IA64::S_LTOFF_DTPREL, "ia64-ltoff-dtprel"},
      {IA64::S_IPLT, "ia64-iplt"},
  };
  return ArrayRef(Flags);
}

// The first operand of a call is the BR that receives the return address,
// and the callee follows it.
const MachineOperand &
IA64InstrInfo::getCalleeOperand(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case IA64::BR_CALL:
  case IA64::BR_CALL_B:
  case IA64::BRL_CALL:
    return MI.getOperand(1);
  default:
    return TargetInstrInfo::getCalleeOperand(MI);
  }
}

// A nop that fits any slot type, so it does not constrain the bundle's
// template.
MCInst IA64InstrInfo::getNop() const {
  return MCInstBuilder(IA64::NOP).addImm(0).addReg(IA64::P0);
}

bool IA64InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  switch (MI.getOpcode()) {
  default:
    return false;
  case IA64::FRAMEADDR: {
    const auto *FI = MBB.getParent()->getInfo<IA64MachineFunctionInfo>();
    BuildMI(MBB, MI, DL, get(IA64::ADDS), MI.getOperand(0).getReg())
        .addImm(0)
        .addReg(FI->getFramePointer())
        .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  }
  case IA64::GET_FPSR:
    BuildMI(MBB, MI, DL, get(IA64::MOV_FROM_AR), MI.getOperand(0).getReg())
        .addReg(IA64::AR40)
        .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  case IA64::SET_FPSR:
    BuildMI(MBB, MI, DL, get(IA64::MOV_AR), IA64::AR40)
        .add(MI.getOperand(0))
        .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  case IA64::SET_LC:
    BuildMI(MBB, MI, DL, get(IA64::MOV_AR), IA64::AR65)
        .add(MI.getOperand(0))
        .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  case IA64::CMPXCHG: {
    static const unsigned Opcodes[] = {IA64::CMPXCHG1_ACQ, IA64::CMPXCHG2_ACQ,
                                       IA64::CMPXCHG4_ACQ, IA64::CMPXCHG8_ACQ};
    BuildMI(MBB, MI, DL, get(IA64::MOV_AR), IA64::AR32)
        .add(MI.getOperand(2))
        .addReg(IA64::P0);
    BuildMI(MBB, MI, DL, get(Opcodes[Log2_64(MI.getOperand(4).getImm())]))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .add(MI.getOperand(3))
        .addImm(0)
        .addReg(IA64::P0)
        .addReg(IA64::AR32, RegState::Implicit | RegState::Kill);
    MI.eraseFromParent();
    return true;
  }
  case IA64::FMA_S_MERGE:
  case IA64::FMA_D_MERGE:
    BuildMI(
        MBB, MI, DL,
        get(MI.getOpcode() == IA64::FMA_D_MERGE ? IA64::FMA_D : IA64::FMA_S),
        MI.getOperand(0).getReg())
        .add(MI.getOperand(2))
        .add(MI.getOperand(3))
        .add(MI.getOperand(4))
        .add(MI.getOperand(5))
        .add(MI.getOperand(6))
        .addReg(MI.getOperand(0).getReg(), RegState::Implicit)
        .setMIFlags(MI.getFlags());
    MI.eraseFromParent();
    return true;
  case IA64::ADJDYNALLOC: {
    const MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    uint64_t Offset = alignTo(MFI.getMaxCallFrameSize(), 16);
    Register Reg = MI.getOperand(0).getReg();
    // The outgoing argument area can exceed the adds immediate range.
    if (isInt<14>(Offset)) {
      BuildMI(MBB, MI, DL, get(IA64::ADDS), Reg)
          .addImm(Offset)
          .addReg(IA64::R12)
          .addReg(IA64::P0);
    } else {
      if (isInt<22>(Offset))
        BuildMI(MBB, MI, DL, get(IA64::ADDL), Reg)
            .addImm(Offset)
            .addReg(IA64::R0)
            .addReg(IA64::P0);
      else
        BuildMI(MBB, MI, DL, get(IA64::MOVL), Reg)
            .addImm(Offset)
            .addReg(IA64::P0);
      BuildMI(MBB, MI, DL, get(IA64::ADD), Reg)
          .addReg(Reg, RegState::Kill)
          .addReg(IA64::R12)
          .addReg(IA64::P0);
    }
    MI.eraseFromParent();
    return true;
  }
  case IA64::DYNAREAOFFSET: {
    const MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    uint64_t Offset = alignTo(MFI.getMaxCallFrameSize(), 16);
    Register Reg = MI.getOperand(0).getReg();
    if (isInt<22>(Offset))
      BuildMI(MBB, MI, DL, get(IA64::ADDL), Reg)
          .addImm(Offset)
          .addReg(IA64::R0)
          .addReg(IA64::P0);
    else
      BuildMI(MBB, MI, DL, get(IA64::MOVL), Reg)
          .addImm(Offset)
          .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  }
  case IA64::CALL:
  case IA64::CALL_IND: {
    // br.call.sptk.many b0 = target, then restore gp from its saved copy.
    // An indirect call first moves the entry point to b6 and the callee's GP
    // to r1.
    bool Indirect = MI.getOpcode() == IA64::CALL_IND;
    unsigned GPIdx = Indirect ? 2 : 1;
    MachineInstrBuilder Call;
    if (Indirect) {
      // The pseudo reads all its registers at once, but the expansion reads
      // them in sequence, so a register that a later operand also names (as
      // the GP or an argument) is not killed by its first reader.
      auto Read = [&](unsigned Idx) {
        const MachineOperand &MO = MI.getOperand(Idx);
        bool Kill = MO.isKill();
        for (const MachineOperand &Later : drop_begin(MI.operands(), Idx + 1))
          if (Later.isReg() && Later.getReg() && Later.readsReg() &&
              RI.regsOverlap(Later.getReg(), MO.getReg()))
            Kill = false;
        return getKillRegState(Kill) | getUndefRegState(MO.isUndef());
      };
      BuildMI(MBB, MI, DL, get(IA64::MOV_BR), IA64::B6)
          .addReg(MI.getOperand(0).getReg(), Read(0))
          .addImm(0)
          .addReg(IA64::P0);
      BuildMI(MBB, MI, DL, get(IA64::ADDS), IA64::R1)
          .addImm(0)
          .addReg(MI.getOperand(1).getReg(), Read(1))
          .addReg(IA64::P0);
      Call = BuildMI(MBB, MI, DL, get(IA64::BR_CALL_B), IA64::B0)
                 .addReg(IA64::B6, RegState::Kill);
    } else {
      Call = BuildMI(MBB, MI, DL, get(IA64::BR_CALL), IA64::B0)
                 .add(MI.getOperand(0));
    }
    Call.addImm(IA64II::WhetherSPTK).addImm(1).addImm(0).addReg(IA64::P0);
    Call.setMIFlags(MI.getFlags());
    // The argument registers were explicit operands of the pseudo.
    for (const MachineOperand &MO : drop_begin(MI.operands(), GPIdx + 1)) {
      if (!MO.isReg())
        Call.add(MO);
      else if (!(MO.isDef() &&
                 (MO.getReg() == IA64::B0 || MO.getReg() == IA64::B6)))
        Call.addReg(MO.getReg(), (MO.isDef() ? RegState::ImplicitDefine
                                             : RegState::Implicit) |
                                     getKillRegState(MO.isKill()) |
                                     getDeadRegState(MO.isDead()) |
                                     getUndefRegState(MO.isUndef()));
    }
    const MachineOperand &GP = MI.getOperand(GPIdx);
    if (GP.getReg() != IA64::R0)
      BuildMI(MBB, MI, DL, get(IA64::ADDS), IA64::R1)
          .addImm(0)
          .addReg(GP.getReg(), getKillRegState(GP.isKill()))
          .addReg(IA64::P0);
    MI.eraseFromParent();
    return true;
  }
  case IA64::TCRETURN:
  case IA64::TCRETURN_IND: {
    // An indirect tail call first moves the entry point to b6 and the
    // callee's GP to r1, while the registers holding them are still in the
    // frame.
    bool Indirect = MI.getOpcode() == IA64::TCRETURN_IND;
    unsigned InputsIdx = Indirect ? 2 : 1;
    if (Indirect) {
      const MachineOperand &Entry = MI.getOperand(0);
      const MachineOperand &GP = MI.getOperand(1);
      bool EntryKill =
          Entry.isKill() && !RI.regsOverlap(Entry.getReg(), GP.getReg());
      BuildMI(MBB, MI, DL, get(IA64::MOV_BR), IA64::B6)
          .addReg(Entry.getReg(), getKillRegState(EntryKill))
          .addImm(0)
          .addReg(IA64::P0);
      BuildMI(MBB, MI, DL, get(IA64::ADDS), IA64::R1)
          .addImm(0)
          .addReg(GP.getReg(), getKillRegState(GP.isKill()))
          .addReg(IA64::P0);
    }
    // Shrink the register stack frame to just the outputs, which is the
    // frame a call would give the callee. A callee that allocates no frame
    // of its own then passes the outputs on as they are. A function that
    // allocated none itself still has the frame it was given, which holds
    // at least its inputs; if the outputs are among them, the callee can
    // have that frame as it is, as the next one after it can.
    // A callee that does not need that frame can have the function's own,
    // which has the arguments where the callee looks for them.
    unsigned Outputs = MI.getOperand(InputsIdx).getImm();
    bool KeepsFrame = Outputs & IA64II::TailKeepsFrame;
    Outputs &= ~IA64II::TailKeepsFrame;
    const MachineFunction &MF = *MBB.getParent();
    bool HasFrame = MF.getInfo<IA64MachineFunctionInfo>()->getSavedPFS();
    unsigned Inputs = 0;
    for (auto [Reg, VReg] : MF.getRegInfo().liveins())
      if (Reg >= IA64::R32 && Reg <= IA64::R39)
        Inputs = std::max(Inputs, unsigned(Reg - IA64::R32 + 1));
    if (!KeepsFrame && (HasFrame || Outputs > Inputs))
      BuildMI(MBB, MI, DL, get(IA64::ALLOC), IA64::R2)
          .addImm(0)
          .addImm(0)
          .addImm(Outputs)
          .addImm(0)
          .addReg(IA64::P0)
          ->getOperand(0)
          .setIsDead();
    MachineInstrBuilder Branch =
        Indirect
            ? BuildMI(MBB, MI, DL, get(IA64::BR_TAIL_B))
                  .addReg(IA64::B6, RegState::Kill)
            : BuildMI(MBB, MI, DL, get(IA64::BR_TAIL)).add(MI.getOperand(0));
    Branch.addImm(IA64II::WhetherSPTK).addImm(1).addImm(0).addReg(IA64::P0);
    if (Indirect)
      Branch.addReg(IA64::R1, RegState::Implicit);
    // Carry over the argument register operands, including undef ones.
    for (MachineOperand MO : drop_begin(MI.operands(), InputsIdx + 1)) {
      if (!MO.isReg())
        continue;
      MO.setImplicit();
      Branch.add(MO);
    }
    Branch.setMIFlags(MI.getFlags());
    MI.eraseFromParent();
    return true;
  }
  case IA64::SETCC:
  case IA64::SETCCi:
  case IA64::SETCCf:
  case IA64::SETCCfq:
  case IA64::SETCCfs:
  case IA64::SETCC_AND:
  case IA64::SETCC_OR:
  case IA64::SETCC_ANDf:
  case IA64::SETCC_ORf: {
    // Set the result to 0, then to 1 under p6. The result may be an operand
    // register, so the compares go first; the last can still share an
    // instruction group with the 0, since a write after a read is not a
    // dependency.
    Register Dst = MI.getOperand(0).getReg();
    CompareKind Kind = MI.getOpcode() == IA64::SETCCfq   ? QuietCompare
                       : MI.getOpcode() == IA64::SETCCfs ? SignalingCompare
                                                         : PlainCompare;
    emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(1).getImm()),
                MI.getOperand(2), MI.getOperand(3), Kind);
    if (std::optional<bool> And = isAndChain(MI.getOpcode()))
      emitChainCompares(MBB, MI, DL, 4, *And);
    BuildMI(MBB, MI, DL, get(IA64::ADDS), Dst)
        .addImm(0)
        .addReg(IA64::R0)
        .addReg(IA64::P0);
    BuildMI(MBB, MI, DL, get(IA64::ADDL), Dst)
        .addImm(1)
        .addReg(IA64::R0)
        .addReg(IA64::P6)
        .addReg(Dst, RegState::Implicit);
    break;
  }
  case IA64::SETFCLASS: {
    // The same, using the predicate for "in the class" or for "not in the
    // class".
    Register Dst = MI.getOperand(0).getReg();
    bool None = MI.getOperand(3).getImm();
    BuildMI(MBB, MI, DL, get(IA64::FCLASS_M))
        .addReg(None ? IA64::P7 : IA64::P6, RegState::Define)
        .addReg(None ? IA64::P6 : IA64::P7, RegState::Define)
        .addReg(MI.getOperand(1).getReg())
        .addImm(MI.getOperand(2).getImm())
        .addReg(IA64::P0);
    BuildMI(MBB, MI, DL, get(IA64::ADDS), Dst)
        .addImm(0)
        .addReg(IA64::R0)
        .addReg(IA64::P0);
    BuildMI(MBB, MI, DL, get(IA64::ADDL), Dst)
        .addImm(1)
        .addReg(IA64::R0)
        .addReg(IA64::P6)
        .addReg(Dst, RegState::Implicit);
    break;
  }
  case IA64::SELECT:
  case IA64::SELECTi:
  case IA64::SELECTf:
  case IA64::SELECT_FR:
  case IA64::SELECT_FRi:
  case IA64::SELECT_FRf:
  case IA64::SELECT_AND:
  case IA64::SELECT_OR:
  case IA64::SELECT_FR_AND:
  case IA64::SELECT_FR_OR:
  case IA64::SELECT_ANDf:
  case IA64::SELECT_ORf:
  case IA64::SELECT_FR_ANDf:
  case IA64::SELECT_FR_ORf: {
    Register Dst = MI.getOperand(0).getReg();
    Register True = MI.getOperand(2).getReg();
    // The value kept when the predicate is false may be undef.
    auto Kept =
        RegState::Implicit | getUndefRegState(MI.getOperand(1).isUndef());
    emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(3).getImm()),
                MI.getOperand(4), MI.getOperand(5));
    if (std::optional<bool> And = isAndChain(MI.getOpcode()))
      emitChainCompares(MBB, MI, DL, 6, *And);
    if (IA64::FRRegClass.contains(Dst))
      BuildMI(MBB, MI, DL, get(IA64::FMERGE_S), Dst)
          .addReg(True)
          .addReg(True)
          .addReg(IA64::P6)
          .addReg(Dst, Kept);
    else
      BuildMI(MBB, MI, DL, get(IA64::ADDS), Dst)
          .addImm(0)
          .addReg(True)
          .addReg(IA64::P6)
          .addReg(Dst, Kept);
    break;
  }
  }
  MI.eraseFromParent();
  return true;
}

bool IA64InstrInfo::cancelsFill(const MachineInstr &Load,
                                const MachineInstr &Store, AAResults *AA) {
  if (!Load.mayLoad() || Load.mayStore() || Load.getNumExplicitDefs() == 0 ||
      !Store.mayStore() || Store.isCall())
    return false;
  const MachineOperand &Def = Load.getOperand(0);
  if (!Def.isReg() || !Def.isDef() || !IA64::GRRegClass.contains(Def.getReg()))
    return false;
  // Only a store to the bytes the load read cancels the fill, so a store
  // that merely may alias them is left alone: keeping it out of the load
  // use's group would cost a stop wherever two buffers might overlap.
  if (!Load.hasOneMemOperand() || !Store.hasOneMemOperand())
    return false;
  const MachineMemOperand &L = **Load.memoperands_begin();
  const MachineMemOperand &S = **Store.memoperands_begin();
  if (!L.getSize().hasValue() || L.getSize().isScalable() ||
      !S.getSize().hasValue() || S.getSize().isScalable())
    return false;
  int64_t LBegin = L.getOffset(), SBegin = S.getOffset();
  int64_t LEnd = LBegin + L.getSize().getValue().getFixedValue();
  int64_t SEnd = SBegin + S.getSize().getValue().getFixedValue();
  const Value *LV = L.getValue(), *SV = S.getValue();
  const PseudoSourceValue *LP = L.getPseudoValue(), *SP = S.getPseudoValue();
  if ((LV && LV == SV) || (LP && LP == SP))
    return LBegin < SEnd && SBegin < LEnd;
  if (!AA || !LV || !SV)
    return false;
  int64_t Min = std::min(LBegin, SBegin);
  AliasResult R = AA->alias(
      MemoryLocation(LV, LocationSize::precise(LEnd - Min), L.getAAInfo()),
      MemoryLocation(SV, LocationSize::precise(SEnd - Min), S.getAAInfo()));
  return R == AliasResult::MustAlias || R == AliasResult::PartialAlias;
}

bool IA64InstrInfo::isConditionalBranch(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case IA64::BRCC:
  case IA64::BRCCi:
  case IA64::BRCCf:
    return true;
  default:
    return false;
  }
}

// A branch condition is three operands: the condition code, the LHS and the
// RHS.
static void getCondition(const MachineInstr &MI,
                         SmallVectorImpl<MachineOperand> &Cond) {
  for (unsigned I = 1; I <= 3; ++I)
    Cond.push_back(MI.getOperand(I));
}

bool IA64InstrInfo::isPredicated(const MachineInstr &MI) const {
  int Idx = MI.findFirstPredOperandIdx();
  return Idx >= 0 && MI.getOperand(Idx).getReg() != IA64::P0;
}

// An addl from r0 materializes a constant in one A-unit instruction, so the
// register coalescer recomputes it at a copy rather than keeping it live,
// which in a large function is a stacked register through every call.
bool IA64InstrInfo::isAsCheapAsAMove(const MachineInstr &MI) const {
  if (MI.getOpcode() == IA64::ADDL && MI.getOperand(1).isImm() &&
      MI.getOperand(2).getReg() == IA64::R0)
    return true;
  return TargetInstrInfo::isAsCheapAsAMove(MI);
}

// In a loop, MachineSink sinks into a block that runs whenever MI's block
// does to shorten live ranges. That moves MI past the branch that ends its
// block, so it can no longer issue alongside the long-latency work before the
// branch, and the chain it starts is added to every iteration after it. The
// join of an if-then or if-then-else is such a block.
bool IA64InstrInfo::isSafeToSink(MachineInstr &MI,
                                 MachineBasicBlock *SuccToSinkTo,
                                 MachineCycleInfo *CI) const {
  if (!CI->getCycle(MI.getParent()))
    return true;
  return !all_of(MI.getParent()->successors(), [&](MachineBasicBlock *Succ) {
    return Succ == SuccToSinkTo ||
           (Succ->succ_size() == 1 && *Succ->succ_begin() == SuccToSinkTo);
  });
}

bool IA64::isControlSpeculativeLoad(unsigned Opcode) {
  switch (Opcode) {
  case IA64::LD1_S:
  case IA64::LD2_S:
  case IA64::LD4_S:
  case IA64::LD8_S:
    return true;
  default:
    return false;
  }
}

// A control-speculative load is up above the branches to start early.
bool IA64InstrInfo::shouldSink(const MachineInstr &MI) const {
  return !IA64::isControlSpeculativeLoad(MI.getOpcode());
}

// alloc must not move: it has to precede the first write of a register it
// adds to the frame and follow the last read of one it removes, which its
// operands do not express. Frame setup and destroy instructions also stay
// put, since the unwind directives describe them by position, except for
// the restores of ar.pfs, ar.lc and the branch registers, which they do not
// describe. The cycles of a software-pipelined kernel stay in order too.
bool IA64InstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                         const MachineBasicBlock *MBB,
                                         const MachineFunction &MF) const {
  if (MI.getOpcode() == IA64::ALLOC || MI.getFlag(MachineInstr::FrameSetup) ||
      MI.getAsmPrinterFlag(IA64::StartsCycle))
    return true;
  // The epilogue's fills and their addresses may also be scheduled among the
  // block, so that the I0-only restores are not left to a region of their
  // own; only the restores of sp and ar.unat stay where they are.
  if (MI.getFlag(MachineInstr::FrameDestroy) &&
      MI.getOpcode() != IA64::MOV_AR_RESTORE &&
      MI.getOpcode() != IA64::MOV_BR && MI.getOpcode() != IA64::LDF_FILL &&
      MI.getOpcode() != IA64::LD8_FILL &&
      (MI.getOpcode() != IA64::ADDS ||
       MI.modifiesRegister(IA64::R12, /*TRI=*/nullptr)))
    return true;
  return TargetInstrInfo::isSchedulingBoundary(MI, MBB, MF);
}

bool IA64InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  // A predicated return cannot be analyzed: its predicate cannot be turned
  // back into a compare.
  for (const MachineInstr &MI : MBB.terminators())
    if (MI.isReturn() && isPredicated(MI))
      return true;

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  MachineInstr *Last = &*I;
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (Last->getOpcode() == IA64::BRUNCOND) {
      TBB = Last->getOperand(0).getMBB();
      return false;
    }
    if (isConditionalBranch(*Last)) {
      TBB = Last->getOperand(0).getMBB();
      getCondition(*Last, Cond);
      return false;
    }
    if (Last->getOpcode() == IA64::BRLOOP) {
      TBB = Last->getOperand(0).getMBB();
      Cond.push_back(Last->getOperand(1));
      return false;
    }
    return true;
  }

  MachineInstr *SecondLast = &*I;
  if (I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  if (isConditionalBranch(*SecondLast) && Last->getOpcode() == IA64::BRUNCOND) {
    TBB = SecondLast->getOperand(0).getMBB();
    getCondition(*SecondLast, Cond);
    FBB = Last->getOperand(0).getMBB();
    return false;
  }
  if (SecondLast->getOpcode() == IA64::BRLOOP &&
      Last->getOpcode() == IA64::BRUNCOND) {
    TBB = SecondLast->getOperand(0).getMBB();
    Cond.push_back(SecondLast->getOperand(1));
    FBB = Last->getOperand(0).getMBB();
    return false;
  }

  // A br.ctop cannot be rebuilt with its rotating registers, so it is left
  // unanalyzed, but a branch after it to the block it falls through to, which
  // moving its block leaves behind, can go.
  if (SecondLast->getOpcode() == IA64::BR_CTOP &&
      Last->getOpcode() == IA64::BRUNCOND &&
      MBB.isLayoutSuccessor(Last->getOperand(0).getMBB())) {
    if (AllowModify)
      Last->eraseFromParent();
    return true;
  }

  // Nothing reaches the second of two unconditional branches.
  if (SecondLast->getOpcode() == IA64::BRUNCOND &&
      Last->getOpcode() == IA64::BRUNCOND) {
    TBB = SecondLast->getOperand(0).getMBB();
    if (AllowModify)
      Last->eraseFromParent();
    return false;
  }
  return true;
}

unsigned IA64InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;
  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  while (I != MBB.end() &&
         (I->getOpcode() == IA64::BRUNCOND || I->getOpcode() == IA64::BRLOOP ||
          isConditionalBranch(*I))) {
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    ++Count;
    I = MBB.getLastNonDebugInstr();
  }
  return Count;
}

unsigned IA64InstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && "A branch needs somewhere to go");
  assert((Cond.size() <= 1 || Cond.size() == 3) && "Not a condition");

  if (Cond.empty()) {
    assert(!FBB && "Two destinations and no condition");
    MachineInstr *MI = BuildMI(&MBB, DL, get(IA64::BRUNCOND)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded = getInstSizeInBytes(*MI);
    return 1;
  }

  unsigned Opcode = IA64::BRCC;
  if (Cond.size() == 1)
    Opcode = IA64::BRLOOP;
  else if (Cond[2].isImm())
    Opcode = IA64::BRCCi;
  else if (IA64CC::isFloatingPoint(IA64CC::CondCode(Cond[0].getImm())))
    Opcode = IA64::BRCCf;
  MachineInstr *MI = BuildMI(&MBB, DL, get(Opcode)).addMBB(TBB).add(Cond);
  if (BytesAdded)
    *BytesAdded = getInstSizeInBytes(*MI);
  if (!FBB)
    return 1;
  MI = BuildMI(&MBB, DL, get(IA64::BRUNCOND)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MI);
  return 2;
}

// Each condition has an opposite, and both accept the same immediates. A
// counted loop branch either continues the loop or leaves it.
bool IA64InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() == 1) {
    Cond[0].setImm(!Cond[0].getImm());
    return false;
  }
  assert(Cond.size() == 3 && "Not a condition");
  Cond[0].setImm(IA64CC::getOpposite(IA64CC::CondCode(Cond[0].getImm())));
  return false;
}

// The most Reg can hold where MBB starts, as far as an unsigned compare with
// a constant of it, or of it plus a constant without wrapping, that every way
// into MBB has passed bounds it.
static std::optional<uint64_t> getGuardedMax(const MachineRegisterInfo &MRI,
                                             Register Reg,
                                             const MachineBasicBlock *MBB) {
  for (unsigned I = 0; I != 4 && MBB->pred_size() == 1; ++I) {
    const MachineBasicBlock *Pred = *MBB->pred_begin();
    if (Pred->succ_size() == 2)
      for (const MachineInstr &Br : Pred->terminators()) {
        if (Br.getOpcode() != IA64::BRCCi)
          continue;
        Register Tested = Br.getOperand(2).getReg();
        uint64_t Offset = 0;
        if (Tested != Reg) {
          const MachineInstr *Add = MRI.getUniqueVRegDef(Tested);
          if (!Add || Add->getOpcode() != IA64::ADDS ||
              !Add->getFlag(MachineInstr::NoUWrap) ||
              Add->getOperand(1).getImm() < 0 ||
              Add->getOperand(2).getReg() != Reg)
            continue;
          Offset = Add->getOperand(1).getImm();
        }
        auto CC = IA64CC::CondCode(Br.getOperand(1).getImm());
        if (Br.getOperand(0).getMBB() != MBB)
          CC = IA64CC::getOpposite(CC);
        uint64_t C = Br.getOperand(3).getImm();
        if (CC == IA64CC::LTU && C) {
          CC = IA64CC::LEU;
          --C;
        }
        if ((CC == IA64CC::EQ || CC == IA64CC::LEU) && C >= Offset)
          return C - Offset;
      }
    MBB = Pred;
  }
  return std::nullopt;
}

// The most Reg can hold where MBB starts, as far as masks, extracts, small
// adds and compares that guard MBB bound it, such as the count of a remainder
// loop that loop unrolling leaves, or of an inner loop that runs to a constant
// from an outer loop's index.
static std::optional<uint64_t> getMaxValue(const MachineRegisterInfo &MRI,
                                           Register Reg,
                                           const MachineBasicBlock *MBB,
                                           unsigned Depth = 0) {
  if (!Reg.isVirtual() || Depth > 4)
    return std::nullopt;
  const MachineInstr *MI = MRI.getUniqueVRegDef(Reg);
  if (!MI)
    return std::nullopt;
  std::optional<uint64_t> Max = getGuardedMax(MRI, Reg, MBB);
  auto Bound = [&](std::optional<uint64_t> Def) {
    if (Def && (!Max || *Def < *Max))
      Max = Def;
    return Max;
  };
  const MachineBasicBlock *DefBB = MI->getParent();
  switch (MI->getOpcode()) {
  case IA64::COPY:
    return Bound(
        getMaxValue(MRI, MI->getOperand(1).getReg(), DefBB, Depth + 1));
  case IA64::ANDi:
    if (MI->getOperand(1).getImm() >= 0)
      return Bound(MI->getOperand(1).getImm());
    break;
  case IA64::EXTR_U:
    return Bound(maskTrailingOnes<uint64_t>(MI->getOperand(3).getImm()));
  case IA64::ZXT1:
    return Bound(0xff);
  case IA64::ZXT2:
    return Bound(0xffff);
  case IA64::ZXT4:
    return Bound(0xffffffff);
  case IA64::ADDP4:
    if (MI->getOperand(2).getReg() == IA64::R0)
      return Bound(0xffffffff);
    break;
  case IA64::ADDS: {
    // An add that cannot wrap past zero.
    int64_t Imm = MI->getOperand(1).getImm();
    std::optional<uint64_t> In =
        getMaxValue(MRI, MI->getOperand(2).getReg(), DefBB, Depth + 1);
    if (In && *In <= (uint64_t)INT64_MAX && (int64_t)*In + Imm >= 0)
      return Bound(*In + Imm);
    break;
  }
  case IA64::SUBi: {
    // A constant less a value no more than it.
    int64_t Imm = MI->getOperand(1).getImm();
    std::optional<uint64_t> In =
        getMaxValue(MRI, MI->getOperand(2).getReg(), DefBB, Depth + 1);
    if (Imm >= 0 && In && *In <= (uint64_t)Imm)
      return Bound(Imm);
    break;
  }
  case IA64::PHI: {
    uint64_t PhiMax = 0;
    for (unsigned I = 1, E = MI->getNumOperands(); I < E; I += 2) {
      std::optional<uint64_t> In =
          getMaxValue(MRI, MI->getOperand(I).getReg(),
                      MI->getOperand(I + 1).getMBB(), Depth + 1);
      if (!In)
        return Max;
      PhiMax = std::max(PhiMax, *In);
    }
    return Bound(PhiMax);
  }
  }
  return Max;
}

namespace {
// A loop counted in ar.lc is the only kind pipelined. The count is set in the
// preheader, and each prologue closes with a br.cloop that leaves for its
// epilogue once the count has run out, so the prologues use up one count
// each for the iterations they start, and the count needs no adjusting.
//
// The MVE expander instead unrolls the kernel so that no value needs copying
// from one stage's register to the next, and runs whatever iterations are
// left over in the original loop. Its pipelined loop counts in a general
// register, starting from the value the preheader sets ar.lc to, and leaves
// ar.lc alone until the epilogue sets it for the iterations left over.
class IA64PipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  const IA64InstrInfo &TII;
  MachineRegisterInfo &MRI;
  MachineBasicBlock *LoopBB;
  // One less than the number of iterations, which the preheader puts in
  // ar.lc.
  Register InitLC;
  // The number of iterations the kernel of the pipelined loop leaves to
  // start: on entry, the count after its first pass, and afterwards the
  // count after its last.
  Register Remaining;

  // Reg plus Imm, in a new register at the end of MBB.
  Register addImm(MachineBasicBlock &MBB, Register Reg, int64_t Imm) {
    Register Sum = MRI.createVirtualRegister(&IA64::GRRegClass);
    MachineBasicBlock::iterator I = MBB.getFirstTerminator();
    if (isInt<14>(Imm)) {
      BuildMI(MBB, I, DebugLoc(), TII.get(IA64::ADDS), Sum)
          .addImm(Imm)
          .addReg(Reg)
          .addReg(IA64::P0);
      return Sum;
    }
    Register ImmReg = MRI.createVirtualRegister(&IA64::GRRegClass);
    TII.loadImmediate(MBB, I, DebugLoc(), ImmReg, Imm);
    BuildMI(MBB, I, DebugLoc(), TII.get(IA64::ADD), Sum)
        .addReg(Reg)
        .addReg(ImmReg)
        .addReg(IA64::P0);
    return Sum;
  }

  // The condition that Reg compares as CC with Imm.
  void compare(MachineBasicBlock &MBB, IA64CC::CondCode CC, Register Reg,
               int64_t Imm, SmallVectorImpl<MachineOperand> &Cond) {
    Cond.push_back(MachineOperand::CreateImm(CC));
    Cond.push_back(MachineOperand::CreateReg(Reg, /*isDef=*/false));
    if (IA64CC::isLegalImmediate(CC, Imm)) {
      Cond.push_back(MachineOperand::CreateImm(Imm));
      return;
    }
    Register ImmReg = MRI.createVirtualRegister(&IA64::GRRegClass);
    TII.loadImmediate(MBB, MBB.getFirstTerminator(), DebugLoc(), ImmReg, Imm);
    Cond.push_back(MachineOperand::CreateReg(ImmReg, /*isDef=*/false));
  }

public:
  IA64PipelinerLoopInfo(const IA64InstrInfo &TII, MachineRegisterInfo &MRI,
                        MachineBasicBlock *LoopBB, Register InitLC)
      : TII(TII), MRI(MRI), LoopBB(LoopBB), InitLC(InitLC) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override {
    return MI->isTerminator();
  }

  std::optional<bool> createTripCountGreaterCondition(
      int TC, MachineBasicBlock &MBB,
      SmallVectorImpl<MachineOperand> &Cond) override {
    Cond.push_back(MachineOperand::CreateImm(1));
    return std::nullopt;
  }

  void adjustTripCount(int TripCountAdjust) override {}
  void setPreheader(MachineBasicBlock *NewPreheader) override {}

  bool isMVEExpanderSupported() override { return InitLC.isValid(); }
  bool isMVEExpanderPreferred() override { return true; }

  // A loop that can run its kernel alone, in rotating registers, may have
  // more stages than one that must be unrolled. One that is not to rotate
  // is left to the default expander, however few stages it has.
  unsigned getMaxStages() override {
    std::optional<uint64_t> MaxTripCount;
    if (InitLC.isValid())
      if (std::optional<uint64_t> MaxLC =
              getMaxValue(MRI, InitLC, MRI.getVRegDef(InitLC)->getParent()))
        MaxTripCount = *MaxLC + 1;
    return getIA64RotatingMaxStages(LoopBB, MaxTripCount);
  }
  bool expandSchedule(ModuloSchedule &MS, LiveIntervals &LIS) override {
    return getMaxStages() && expandIA64RotatingSchedule(MS, LIS);
  }

  // The expander asks first, before the prologue, whether the loop runs more
  // than the TC iterations that the prologue and one pass of the kernel
  // start; then, at the end of the kernel, whether more than TC are left,
  // for another pass; and last, in the epilogue, whether any are left.
  void createRemainingIterationsGreaterCondition(
      int TC, MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &Cond,
      DenseMap<MachineInstr *, MachineInstr *> &LastStage0Insts) override {
    if (LastStage0Insts.empty()) {
      MRI.clearKillFlags(InitLC);
      Remaining = addImm(MBB, InitLC, -TC);
      compare(MBB, IA64CC::GEU, InitLC, TC, Cond);
      return;
    }
    if (LastStage0Insts.begin()->second->getParent() != &MBB) {
      // The original loop runs the rest, counted in ar.lc.
      Register LC = addImm(MBB, Remaining, -1);
      BuildMI(MBB, MBB.getFirstTerminator(), DebugLoc(), TII.get(IA64::SET_LC))
          .addReg(LC);
      compare(MBB, IA64CC::NE, Remaining, 0, Cond);
      return;
    }
    // Each pass of the kernel starts TC + 1 iterations.
    MachineBasicBlock *Entry = *find_if(
        MBB.predecessors(), [&](MachineBasicBlock *P) { return P != &MBB; });
    Register Left = MRI.createVirtualRegister(&IA64::GRRegClass);
    Register Next = addImm(MBB, Left, -(TC + 1));
    BuildMI(MBB, MBB.begin(), DebugLoc(), TII.get(IA64::PHI), Left)
        .addReg(Remaining)
        .addMBB(Entry)
        .addReg(Next)
        .addMBB(&MBB);
    Remaining = Left;
    compare(MBB, IA64CC::GTU, Left, TC, Cond);
  }
};
} // end anonymous namespace

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
IA64InstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
  MachineBasicBlock::iterator I = LoopBB->getFirstTerminator();
  if (I == LoopBB->end() || I->getOpcode() != IA64::BRLOOP)
    return nullptr;
  // Find the count the preheader starts the loop with, for the MVE expander.
  Register InitLC;
  MachineBasicBlock *Preheader = nullptr;
  if (LoopBB->pred_size() == 2)
    Preheader = *find_if(LoopBB->predecessors(),
                         [&](MachineBasicBlock *P) { return P != LoopBB; });
  if (Preheader)
    for (MachineInstr &MI : reverse(*Preheader))
      if (MI.definesRegister(IA64::AR65, /*TRI=*/nullptr)) {
        if (MI.getOpcode() == IA64::SET_LC &&
            MI.getOperand(0).getReg().isVirtual())
          InitLC = MI.getOperand(0).getReg();
        break;
      }
  return std::make_unique<IA64PipelinerLoopInfo>(
      *this, LoopBB->getParent()->getRegInfo(), LoopBB, InitLC);
}

// A load or store addresses memory by the register of its first input, with
// no offset.
static const MachineOperand *getMemBaseOp(const MachineInstr &MI) {
  if (!MI.mayLoadOrStore() || (MI.mayLoad() && MI.mayStore()) ||
      !MI.hasOneMemOperand())
    return nullptr;
  const MachineOperand &Base = MI.getOperand(MI.getNumExplicitDefs());
  return Base.isReg() ? &Base : nullptr;
}

// The immediate a load or store adds to its address register after the
// access. It follows the address of a load and the value of a store, and
// precedes the hint.
static std::optional<int64_t> getPostIncrement(const MachineInstr &MI) {
  const MachineOperand *Base = getMemBaseOp(MI);
  if (!Base || !Base->isTied())
    return std::nullopt;
  unsigned Idx = MI.getOperandNo(Base) + (MI.mayStore() ? 2 : 1);
  if (Idx + 1 >= MI.getNumOperands() || !MI.getOperand(Idx).isImm() ||
      !MI.getOperand(Idx + 1).isImm())
    return std::nullopt;
  return MI.getOperand(Idx).getImm();
}

bool IA64InstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *TRI) const {
  const MachineOperand *Base = getMemBaseOp(MI);
  if (!Base)
    return false;
  // With no offset in the access, an unrolled loop forms each address with
  // an add to a common register. In SSA form, report the access as one at an
  // offset from that register.
  Offset = 0;
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  if (MRI.isSSA() && Base->getReg().isVirtual())
    if (const MachineInstr *Def = MRI.getUniqueVRegDef(Base->getReg()))
      if (Def->getOpcode() == IA64::ADDS && !isPredicated(*Def) &&
          Def->getOperand(1).isImm() && Def->getOperand(2).isReg() &&
          Def->getOperand(2).getReg().isVirtual()) {
        Base = &Def->getOperand(2);
        Offset = Def->getOperand(1).getImm();
      }
  BaseOps.push_back(Base);
  OffsetIsScalable = false;
  Width = (*MI.memoperands_begin())->getSize();
  return true;
}

bool IA64InstrInfo::getIncrementValue(const MachineInstr &MI,
                                      int &Value) const {
  if (isPredicated(MI))
    return false;
  if (MI.getOpcode() == IA64::ADDS) {
    Value = MI.getOperand(1).getImm();
    return true;
  }
  std::optional<int64_t> Inc = getPostIncrement(MI);
  if (!Inc)
    return false;
  Value = *Inc;
  return true;
}

// Summing four values takes three adds, either chained or as two in parallel
// followed by a third. The machine combiner reassociates the chain into the
// parallel form when that shortens the critical path, which on a six-issue
// machine it usually does. FP add is not associative, and is also an fma
// with a constant register.
//
// sub is the inverse of add and is reassociated as one: a chain of
// subtractions is summed and subtracted once.
bool IA64InstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst,
                                                bool Invert) const {
  unsigned Opcode = Inst.getOpcode();
  if (Invert) {
    std::optional<unsigned> Inverse = getInverseOpcode(Opcode);
    if (!Inverse)
      return false;
    Opcode = *Inverse;
  }
  switch (Opcode) {
  default:
    return false;
  case IA64::ADD:
  case IA64::AND:
  case IA64::OR:
  case IA64::XOR:
    return Inst.getOperand(3).getReg() == IA64::P0;
  }
}

std::optional<unsigned> IA64InstrInfo::getInverseOpcode(unsigned Opcode) const {
  switch (Opcode) {
  default:
    return std::nullopt;
  case IA64::ADD:
    return IA64::SUB;
  case IA64::SUB:
    return IA64::ADD;
  }
}

// An instruction's size is unknown until the assembler bundles it, three to
// a 16-byte bundle if the templates and stops allow. This returns the upper
// bound, a whole bundle, for branch relaxation, where overestimating a
// distance is cheap and underestimating it is an error.
unsigned IA64InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  if (MI.isMetaInstruction())
    return 0;
  switch (MI.getOpcode()) {
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction &MF = *MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF.getTarget().getMCAsmInfo());
  }
  // A compare plus a branch, or a move to b6 plus a branch.
  case IA64::BRCC:
  case IA64::BRCCi:
  case IA64::BRIND:
  case IA64::BRLOOP:
    return 32;
  // An FP condition may need a second compare to get the required
  // signaling behavior.
  case IA64::BRCCf:
    return 48;
  // Up to one compare more than it has conditions, and the branch.
  case IA64::BRCC_AND:
    return 16 * ((MI.getNumExplicitOperands() - 1) / 3 + 2);
  default:
    return 16;
  }
}

// Branch displacements are in bundles: 21 bits, or 60 bits for brl, which
// reaches everything.
bool IA64InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                          int64_t BrOffset) const {
  return BranchOpc == IA64::BRLONG || isInt<25>(BrOffset);
}

MachineBasicBlock *
IA64InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  return MI.getOperand(0).getMBB();
}

// brl reaches anywhere without a register, so nothing needs to be saved and
// restored around it.
void IA64InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                         MachineBasicBlock &NewDestBB,
                                         MachineBasicBlock &RestoreBB,
                                         const DebugLoc &DL, int64_t BrOffset,
                                         RegScavenger *RS) const {
  BuildMI(&MBB, DL, get(IA64::BRLONG)).addMBB(&NewDestBB);
}
