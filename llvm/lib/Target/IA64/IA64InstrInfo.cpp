//===-- IA64InstrInfo.cpp - IA64 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
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
  if (!IA64::GRRegClass.hasSubClassEq(RC))
    report_fatal_error("IA64 cannot spill a register of this class yet");
  BuildMI(MBB, MI, DL, get(IA64::ADDS), IA64::R2)
      .addImm(0)
      .addFrameIndex(FrameIndex)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
  BuildMI(MBB, MI, DL, get(IA64::ST8))
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
  if (!IA64::GRRegClass.hasSubClassEq(RC))
    report_fatal_error("IA64 cannot reload a register of this class yet");
  BuildMI(MBB, MI, DL, get(IA64::ADDS), DestReg)
      .addImm(0)
      .addFrameIndex(FrameIndex)
      .addReg(IA64::P0)
      .setMIFlag(Flags);
  BuildMI(MBB, MI, DL, get(IA64::LD8), DestReg)
      .addReg(DestReg, RegState::Kill)
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
  case IA64::SETCC:
  case IA64::SETCCi: {
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
  case IA64::SELECTi: {
    Register Dst = MI.getOperand(0).getReg();
    emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(3).getImm()),
                MI.getOperand(4).getReg(), MI.getOperand(5));
    BuildMI(MBB, MI, DL, get(IA64::ADDS), Dst)
        .addImm(0)
        .addReg(MI.getOperand(2).getReg())
        .addReg(IA64::P6)
        .addReg(Dst, RegState::Implicit);
    break;
  }
  }
  MI.eraseFromParent();
  return true;
}

static bool isConditionalBranch(const MachineInstr &MI) {
  return MI.getOpcode() == IA64::BRCC || MI.getOpcode() == IA64::BRCCi;
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

  BuildMI(&MBB, DL, get(Cond[2].isImm() ? IA64::BRCCi : IA64::BRCC))
      .addMBB(TBB)
      .add(Cond);
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
