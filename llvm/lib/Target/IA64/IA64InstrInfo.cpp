//===-- IA64InstrInfo.cpp - IA64 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstrInfo.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

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
  report_fatal_error("IA64 cannot copy between these registers yet");
}

// A value goes to the frame and comes back through r2, which is reserved to
// hold the address. A general register that is being loaded can hold its own
// address first, which keeps reloads independent of one another.
void IA64InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  unsigned Opc;
  if (IA64::GRRegClass.hasSubClassEq(RC))
    Opc = IA64::ST8;
  else if (IA64::FRRegClass.hasSubClassEq(RC))
    Opc = IA64::STF_SPILL;
  else
    report_fatal_error("IA64 cannot spill a register of this class yet");
  BuildMI(MBB, MI, DL, get(IA64::ADDS), IA64::R2)
      .addImm(0)
      .addFrameIndex(FrameIndex)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(IA64::R2, RegState::Kill)
      .addReg(SrcReg, getKillRegState(IsKill))
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
}

void IA64InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  unsigned Opc;
  Register Addr = DestReg;
  if (IA64::GRRegClass.hasSubClassEq(RC)) {
    Opc = IA64::LD8;
  } else if (IA64::FRRegClass.hasSubClassEq(RC)) {
    Opc = IA64::LDF_FILL;
    Addr = IA64::R2;
  } else {
    report_fatal_error("IA64 cannot reload a register of this class yet");
  }
  BuildMI(MBB, MI, DL, get(IA64::ADDS), Addr)
      .addImm(0)
      .addFrameIndex(FrameIndex)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addReg(Addr, RegState::Kill)
      .addImm(0)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
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
// one: r < c is !(c - 1 < r).
bool IA64CC::isLegalImmediate(CondCode CC, int64_t Imm) {
  switch (CC) {
  case LT:
  case GE:
    return isInt<8>(Imm - 1);
  case LTU:
  case GEU:
    return Imm != 0 && isInt<8>(Imm - 1);
  default:
    return isInt<8>(Imm);
  }
}

void IA64InstrInfo::emitCompare(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, IA64CC::CondCode CC,
                                Register LHS, const MachineOperand &RHS) const {
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
    BuildMI(MBB, I, DL, get(Opcode))
        .addReg(Invert ? IA64::P7 : IA64::P6, RegState::Define)
        .addReg(Invert ? IA64::P6 : IA64::P7, RegState::Define)
        .addReg(Swap ? RHS.getReg() : LHS)
        .addReg(Swap ? LHS : RHS.getReg())
        .addImm(0)
        .addReg(IA64::P0);
    return;
  }

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

  MachineInstrBuilder MIB =
      BuildMI(MBB, I, DL, get(Opcode))
          .addReg(Invert ? IA64::P7 : IA64::P6, RegState::Define)
          .addReg(Invert ? IA64::P6 : IA64::P7, RegState::Define);
  if (IsImm) {
    assert(IA64CC::isLegalImmediate(CC, RHS.getImm()) &&
           "No compare takes this constant");
    MIB.addImm(RHS.getImm() + Adjust).addReg(LHS);
  } else if (Swap) {
    MIB.addReg(RHS.getReg()).addReg(LHS);
  } else {
    MIB.addReg(LHS).addReg(RHS.getReg());
  }
  MIB.addReg(IA64::P0);
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
  case IA64::FDIV_S:
  case IA64::FDIV_D: {
    bool IsDouble = MI.getOpcode() == IA64::FDIV_D;
    unsigned Round = IsDouble ? IA64::FMA_D : IA64::FMA_S;
    Register Y = MI.getOperand(0).getReg();
    Register Q = MI.getOperand(1).getReg();
    Register E = MI.getOperand(2).getReg();
    Register T = MI.getOperand(3).getReg();
    Register P = IA64::P6;
    Register A = MI.getOperand(4).getReg();
    Register B = MI.getOperand(5).getReg();
    // R = M1 * M2 + Addend (or Addend - M1 * M2), predicated on frcpa having
    // produced an approximation.
    auto Step = [&](unsigned Opcode, Register R, Register M1, Register M2,
                    Register Addend, unsigned StatusField = 1) {
      BuildMI(MBB, MI, DL, get(Opcode), R)
          .addReg(M1)
          .addReg(M2)
          .addReg(Addend)
          .addImm(StatusField)
          .addReg(P);
    };
    BuildMI(MBB, MI, DL, get(IA64::FRCPA), Y)
        .addReg(P, RegState::Define)
        .addReg(A)
        .addReg(B)
        .addImm(0)
        .addReg(IA64::P0);
    // With e = 1 - b * y, a / b is a * y / (1 - e), and 1 / (1 - e) is
    // (1 + e) * (1 + e^2) * (1 + e^4) and so on.
    Step(IA64::FMA, Q, A, Y, IA64::F0);
    Step(IA64::FNMA, E, B, Y, IA64::F1);
    Step(IA64::FMA, Q, E, Q, Q);
    Step(IA64::FMA, T, E, E, IA64::F0);
    Step(IA64::FMA, Y, E, Y, Y);
    if (IsDouble) {
      Step(IA64::FMA, Q, T, Q, Q);
      Step(IA64::FMA, E, T, T, IA64::F0);
      Step(IA64::FMA, Y, T, Y, Y);
      std::swap(E, T);
    }
    Step(Round, Q, T, Q, Q);
    Step(IA64::FMA, Y, T, Y, Y);
    Step(IA64::FNMA, E, B, Q, A);
    Step(Round, Y, E, Y, Q, 0);
    MI.eraseFromParent();
    return true;
  }
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
        for (const MachineOperand &Later :
             drop_begin(MI.operands(), Idx + 1))
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
  case IA64::TCRETURN: {
    // Shrink the register stack frame to just the outputs, which is the
    // frame a call would give the callee. A callee that allocates no frame
    // of its own then passes the outputs on as they are.
    BuildMI(MBB, MI, DL, get(IA64::ALLOC), IA64::R2)
        .addImm(0)
        .addImm(0)
        .add(MI.getOperand(1))
        .addImm(0)
        .addReg(IA64::P0)
        ->getOperand(0)
        .setIsDead();
    MachineInstrBuilder Branch = BuildMI(MBB, MI, DL, get(IA64::BR_TAIL))
                                     .add(MI.getOperand(0))
                                     .addImm(IA64II::WhetherSPTK)
                                     .addImm(1)
                                     .addImm(0)
                                     .addReg(IA64::P0);
    // Carry over the argument register operands, including undef ones.
    for (MachineOperand MO : drop_begin(MI.operands(), 2)) {
      if (!MO.isReg())
        continue;
      MO.setImplicit();
      Branch.add(MO);
    }
    MI.eraseFromParent();
    return true;
  }
  case IA64::SETCC:
  case IA64::SETCCi:
  case IA64::SETCCf: {
    // Set the result to 0, then to 1 under p6. The result may be an operand
    // register, so the compare goes first; they can still share an
    // instruction group, since a write after a read is not a dependency.
    Register Dst = MI.getOperand(0).getReg();
    emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(1).getImm()),
                MI.getOperand(2).getReg(), MI.getOperand(3));
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
  case IA64::SELECT_FRf: {
    Register Dst = MI.getOperand(0).getReg();
    Register True = MI.getOperand(2).getReg();
    // The value kept when the predicate is false may be undef.
    auto Kept =
        RegState::Implicit | getUndefRegState(MI.getOperand(1).isUndef());
    emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(3).getImm()),
                MI.getOperand(4).getReg(), MI.getOperand(5));
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

bool IA64InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
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
  assert(!BytesRemoved && "Nothing asks for the size of a branch");
  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  while (I != MBB.end() &&
         (I->getOpcode() == IA64::BRUNCOND || isConditionalBranch(*I))) {
    I->eraseFromParent();
    ++Count;
    I = MBB.getLastNonDebugInstr();
  }
  return Count;
}

unsigned IA64InstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(!BytesAdded && "Nothing asks for the size of a branch");
  assert(TBB && "A branch needs somewhere to go");
  assert((Cond.empty() || Cond.size() == 3) && "Not a condition");

  if (Cond.empty()) {
    assert(!FBB && "Two destinations and no condition");
    BuildMI(&MBB, DL, get(IA64::BRUNCOND)).addMBB(TBB);
    return 1;
  }

  unsigned Opcode = IA64::BRCC;
  if (Cond[2].isImm())
    Opcode = IA64::BRCCi;
  else if (IA64CC::isFloatingPoint(IA64CC::CondCode(Cond[0].getImm())))
    Opcode = IA64::BRCCf;
  BuildMI(&MBB, DL, get(Opcode)).addMBB(TBB).add(Cond);
  if (!FBB)
    return 1;
  BuildMI(&MBB, DL, get(IA64::BRUNCOND)).addMBB(FBB);
  return 2;
}

// Each condition has an opposite, and both accept the same immediates.
bool IA64InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 3 && "Not a condition");
  Cond[0].setImm(IA64CC::getOpposite(IA64CC::CondCode(Cond[0].getImm())));
  return false;
}
