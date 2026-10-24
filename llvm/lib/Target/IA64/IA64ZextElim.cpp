//===-- IA64ZextElim.cpp - Remove redundant zero extensions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An i32 value lives in a 64-bit register whose upper half instruction
// selection knows nothing about once the value crosses a block, so a zext
// of a phi becomes a zxt4 even where every value reaching the phi was loaded
// by an ld4 or shifted right out of one. In a loop that chases a pointer
// stored as a 32-bit index, "t = tt[t] >> 8", the zxt4 is a cycle on the
// recurrence.
//
// This pass bounds how many low bits each value can occupy, through copies
// and phis, and replaces a zxt1, zxt2, zxt4 or "extr.u r = s, 0, len" whose
// source already fits with that source.
//
// A zxt4 that only feeds shift counts, as zstd's bit position does, becomes
// "addp4 r = s, r0" instead, which an M unit can issue as well as an I unit:
// it keeps the low 32 bits and copies bits 30 and 31 to 61 and 62, so the
// count is at least 64 exactly when the zero-extended one is.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-zext-elim"
#define PASS_NAME "IA64 redundant zero extension elimination"

STATISTIC(NumZextsRemoved, "Number of redundant zero extensions removed");
STATISTIC(NumCountZexts, "Number of zxt4 of shift counts made addp4");

namespace {

class IA64ZextElim : public MachineFunctionPass {
  MachineRegisterInfo *MRI = nullptr;
  // Claims (register, bits) being proved. A claim met again along a cycle
  // of phis holds by induction: every value on the cycle is computed from
  // earlier ones by instructions that keep the bound.
  DenseSet<std::pair<Register, unsigned>> Assumed;
  // Definitions a query may still look at.
  unsigned Budget = 0;

public:
  static char ID;

  IA64ZextElim() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool fitsIn(Register Reg, unsigned Bits, unsigned Depth);
  bool onlyShiftCounts(Register Reg) const;
};

} // end anonymous namespace

char IA64ZextElim::ID = 0;

INITIALIZE_PASS(IA64ZextElim, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createIA64ZextElimPass() { return new IA64ZextElim(); }

static unsigned activeBits(int64_t Imm) {
  return Imm < 0 ? 64 : 64 - llvm::countl_zero((uint64_t)Imm);
}

// Whether the value in Reg is known to be below 2^Bits.
bool IA64ZextElim::fitsIn(Register Reg, unsigned Bits, unsigned Depth) {
  if (Bits >= 64)
    return true;
  if (Reg == IA64::R0)
    return true;
  if (!Reg.isVirtual() || Depth > 12 || Budget == 0)
    return false;
  --Budget;
  MachineInstr *MI = MRI->getUniqueVRegDef(Reg);
  if (!MI)
    return false;
  unsigned Opc = MI->getOpcode();
  if (Opc == IA64::PHI) {
    if (!Assumed.insert({Reg, Bits}).second)
      return true;
    bool Fits = true;
    for (unsigned I = 1, E = MI->getNumOperands(); Fits && I < E; I += 2)
      Fits = fitsIn(MI->getOperand(I).getReg(), Bits, Depth + 1);
    Assumed.erase({Reg, Bits});
    return Fits;
  }
  if (Opc == IA64::COPY)
    return fitsIn(MI->getOperand(1).getReg(), Bits, Depth + 1);

  // Everything else must be unpredicated and write Reg first.
  int PredIdx = MI->findFirstPredOperandIdx();
  if (PredIdx < 0 || MI->getOperand(PredIdx).getReg() != IA64::P0 ||
      MI->getOperand(0).getReg() != Reg)
    return false;

  // ld1, ld2 and ld4 in all their forms zero-extend.
  if (MI->mayLoad() && !MI->mayStore() && MI->hasOneMemOperand()) {
    const MachineMemOperand *MMO = *MI->memoperands_begin();
    return MMO->getSize().hasValue() && MMO->getSize().getValue() <= 4 &&
           MMO->getSize().getValue() * 8 <= Bits;
  }

  auto Op = [&](unsigned I) { return MI->getOperand(I); };
  switch (Opc) {
  case IA64::ZXT1:
    return Bits >= 8 || fitsIn(Op(1).getReg(), Bits, Depth + 1);
  case IA64::ZXT2:
    return Bits >= 16 || fitsIn(Op(1).getReg(), Bits, Depth + 1);
  case IA64::ZXT4:
    return Bits >= 32 || fitsIn(Op(1).getReg(), Bits, Depth + 1);
  case IA64::EXTR_U: {
    // r = s{pos+len-1..pos}: below 2^len, and below 2^(b-pos) if s < 2^b.
    unsigned Pos = Op(2).getImm(), Len = Op(3).getImm();
    return Len <= Bits || fitsIn(Op(1).getReg(), Bits + Pos, Depth + 1);
  }
  case IA64::DEP_Z:
    // r = s{len-1..0} << pos.
    return Op(2).getImm() + Op(3).getImm() <= Bits ||
           (Op(2).getImm() <= Bits &&
            fitsIn(Op(1).getReg(), Bits - Op(2).getImm(), Depth + 1));
  case IA64::SHR_U:
    return fitsIn(Op(1).getReg(), Bits, Depth + 1);
  case IA64::AND:
    return fitsIn(Op(1).getReg(), Bits, Depth + 1) ||
           fitsIn(Op(2).getReg(), Bits, Depth + 1);
  case IA64::ANDi:
    return activeBits(Op(1).getImm()) <= Bits ||
           fitsIn(Op(2).getReg(), Bits, Depth + 1);
  case IA64::OR:
  case IA64::XOR:
    return fitsIn(Op(1).getReg(), Bits, Depth + 1) &&
           fitsIn(Op(2).getReg(), Bits, Depth + 1);
  case IA64::ADDS:
  case IA64::ADDL:
    return Op(2).getReg() == IA64::R0 && activeBits(Op(1).getImm()) <= Bits;
  case IA64::MOVL:
    return activeBits(Op(1).getImm()) <= Bits;
  }
  return false;
}

// Whether every use of Reg is the count of a variable shift.
bool IA64ZextElim::onlyShiftCounts(Register Reg) const {
  if (MRI->use_nodbg_empty(Reg))
    return false;
  for (const MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    unsigned Opc = MO.getParent()->getOpcode();
    if ((Opc != IA64::SHL && Opc != IA64::SHR_U && Opc != IA64::SHR) ||
        MO.getOperandNo() != 2)
      return false;
  }
  return true;
}

bool IA64ZextElim::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  MRI = &MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      unsigned Bits;
      switch (MI.getOpcode()) {
      case IA64::ZXT1:
        Bits = 8;
        break;
      case IA64::ZXT2:
        Bits = 16;
        break;
      case IA64::ZXT4:
        Bits = 32;
        break;
      case IA64::EXTR_U:
        if (MI.getOperand(2).getImm() != 0)
          continue;
        Bits = MI.getOperand(3).getImm();
        break;
      default:
        continue;
      }
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      int PredIdx = MI.findFirstPredOperandIdx();
      if (!Dst.isVirtual() || !Src.isVirtual() || PredIdx < 0 ||
          MI.getOperand(PredIdx).getReg() != IA64::P0)
        continue;
      Budget = 256;
      if (!fitsIn(Src, Bits, 0) ||
          !MRI->constrainRegClass(Src, MRI->getRegClass(Dst))) {
        if (MI.getOpcode() == IA64::ZXT4 && onlyShiftCounts(Dst)) {
          LLVM_DEBUG(dbgs() << "Count as addp4: " << MI);
          const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
          BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(IA64::ADDP4), Dst)
              .addReg(Src)
              .addReg(IA64::R0)
              .addReg(IA64::P0);
          MI.eraseFromParent();
          ++NumCountZexts;
          Changed = true;
        }
        continue;
      }
      LLVM_DEBUG(dbgs() << "Removing " << MI);
      MRI->replaceRegWith(Dst, Src);
      MRI->clearKillFlags(Src);
      MI.eraseFromParent();
      ++NumZextsRemoved;
      Changed = true;
    }
  }
  return Changed;
}
