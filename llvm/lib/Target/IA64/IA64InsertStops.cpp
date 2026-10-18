//===-- IA64InsertStops.cpp - Insert stop bits ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instructions between two stops (an instruction group) may execute in
// parallel, so within a group no register may be written and then read, or
// written twice (RAW and WAW dependencies). This pass inserts a stop wherever
// a block would otherwise violate that rule (SDM volume 1, section 3.4, and
// volume 2, section 3.3 of the instruction reference).
//
// It is deliberately simple and conservative at the cost of density: a group
// never spans blocks, and two writes under mutually exclusive predicates,
// which could legally share a group, do not.
//
// The one exception is parallel compares: compares that all set a predicate,
// or all clear it, give the same result in any order and may share a group.
//
// A stop belongs to the instruction it follows, which is how the MC layer
// represents it, so it is recorded as an asm printer flag on the
// MachineInstr.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-insert-stops"
#define PASS_NAME "IA64 stop insertion"

namespace {

class IA64InsertStops : public MachineFunctionPass {
public:
  static char ID;

  IA64InsertStops() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64InsertStops::ID = 0;

INITIALIZE_PASS(IA64InsertStops, DEBUG_TYPE, PASS_NAME, false, false)

// Registers whose value is constant regardless of writes.
static bool isConstantReg(MCRegister Reg) {
  return Reg == IA64::R0 || Reg == IA64::F0 || Reg == IA64::F1 ||
         Reg == IA64::P0;
}

// Parallel compares and bit tests (or.andcm), which set the first predicate and
// clear the second when the condition holds, and write neither otherwise.
static bool isParallelCompare(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case IA64::CMP_EQ_OR_ANDCM:
  case IA64::CMP_EQ_OR_ANDCMi:
  case IA64::CMP_NE_OR_ANDCM:
  case IA64::CMP_NE_OR_ANDCMi:
  case IA64::CMP_GT_OR_ANDCMz:
  case IA64::CMP_GE_OR_ANDCMz:
  case IA64::CMP_LT_OR_ANDCMz:
  case IA64::CMP_LE_OR_ANDCMz:
  case IA64::CMP4_EQ_OR_ANDCM:
  case IA64::CMP4_EQ_OR_ANDCMi:
  case IA64::CMP4_NE_OR_ANDCM:
  case IA64::CMP4_NE_OR_ANDCMi:
  case IA64::CMP4_GT_OR_ANDCMz:
  case IA64::CMP4_GE_OR_ANDCMz:
  case IA64::CMP4_LT_OR_ANDCMz:
  case IA64::CMP4_LE_OR_ANDCMz:
  case IA64::TBIT_Z_OR_ANDCM:
  case IA64::TBIT_NZ_OR_ANDCM:
    return true;
  }
}

bool IA64InsertStops::runOnMachineFunction(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Registers written in the current group, and those written by an F-unit
  // instruction.
  BitVector Written(TRI.getNumRegs());
  BitVector WrittenByF(TRI.getNumRegs());
  // Predicates written in the current group only by parallel compares, split
  // by whether those compares would set or clear them.
  BitVector OnlySet(TRI.getNumRegs());
  BitVector OnlyCleared(TRI.getNumRegs());

  for (MachineBasicBlock &MBB : MF) {
    Written.reset();
    WrittenByF.reset();
    OnlySet.reset();
    OnlyCleared.reset();
    MachineInstr *Last = nullptr;
    // Whether the current group contains an alloc. No register models the
    // current frame marker, which calls and returns replace.
    bool NewFrame = false;
    // Whether the current group may have raised an FP exception. The flags
    // are part of ar.fpsr, but the instructions that set them only declare a
    // read of it (for the rounding mode), so any number may share a group.
    bool RaisedFlags = false;

    auto EndGroup = [&] {
      if (Last)
        Last->setAsmPrinterFlag(IA64::StopAfter);
      Written.reset();
      WrittenByF.reset();
      OnlySet.reset();
      OnlyCleared.reset();
      NewFrame = false;
      RaisedFlags = false;
    };

    for (MachineInstr &MI : MBB) {
      if (MI.isMetaInstruction())
        continue;
      // Inline asm may read and write anything, so it gets a group of its
      // own. The asm printer emits the stop after it.
      if (MI.isInlineAsm()) {
        EndGroup();
        Last = nullptr;
        continue;
      }
      IA64II::Unit Unit = IA64II::getUnit(MI.getDesc().TSFlags);

      // Whether MO counts for the dependency rules. A branch executes last,
      // and its target is outside the group, so implicit operands that only
      // describe liveness across it do not count. A branch may also read a
      // predicate or BR written in its own group, and a return may read
      // ar.pfs, except for a predicate written by an F-unit instruction.
      auto Touches = [&](const MachineOperand &MO) {
        if (!MO.isReg() || !MO.getReg().isPhysical() ||
            isConstantReg(MO.getReg()))
          return false;
        if (Unit != IA64II::UnitB)
          return true;
        MCRegister Reg = MO.getReg();
        if (MO.isDef())
          return !MO.isImplicit() || IA64::ARRegClass.contains(Reg);
        if (IA64::PRRegClass.contains(Reg))
          return WrittenByF.test(Reg.id());
        if (IA64::ARRegClass.contains(Reg))
          return !(Reg == IA64::AR64 && MI.isReturn());
        return false;
      };

      // alloc, calls and returns change RSE state without declaring it, and a
      // read of the RSE registers sees the change only from a later group.
      bool Parallel = isParallelCompare(MI.getOpcode());

      auto ReadsRSE = [](const MachineInstr &MI) {
        if (MI.getOpcode() != IA64::MOV_FROM_AR)
          return false;
        Register AR = MI.getOperand(1).getReg();
        return AR == IA64::AR16 || AR == IA64::AR17 || AR == IA64::AR18 ||
               AR == IA64::AR19;
      };

      // A read of ar.fpsr, or fchkf of a status field's flags, does not see
      // flags raised in its own group, and a write of ar.fpsr or of a status
      // field's flags must not share a group that raised any.
      bool ReadsFlags = MI.getOpcode() == IA64::FCHKF ||
                        (MI.getOpcode() == IA64::MOV_FROM_AR &&
                         MI.getOperand(1).getReg() == IA64::AR40);
      bool UsesFPSR = MI.readsRegister(IA64::AR40, &TRI);
      bool SetsFPSR = MI.definesRegister(IA64::AR40, &TRI);

      // alloc and flushrs must be first in their group. A call or return
      // must not share a group with an alloc; a tail call is exempt, though
      // it acts as both.
      if (MI.getOpcode() == IA64::ALLOC || MI.getOpcode() == IA64::FLUSHRS ||
          ReadsRSE(MI) || (RaisedFlags && (ReadsFlags || SetsFPSR)) ||
          (NewFrame && (MI.isCall() || MI.isReturn()) &&
           MI.getOpcode() != IA64::BR_TAIL) ||
          any_of(MI.operands(), [&](const MachineOperand &MO) {
            if (!Touches(MO) || !Written.test(MO.getReg().id()))
              return false;
            // Another parallel compare writing the predicate the same way
            // as the others in the group.
            if (Parallel && MO.isDef())
              return !(MO.getOperandNo() == 0 ? OnlySet : OnlyCleared)
                          .test(MO.getReg().id());
            return true;
          }))
        EndGroup();
      NewFrame |= MI.getOpcode() == IA64::ALLOC;
      RaisedFlags |= Unit == IA64II::UnitF && UsesFPSR && !ReadsFlags;

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef() || !Touches(MO))
          continue;
        // A predicate first written in the group by a parallel compare stays
        // open to more parallel compares of the same kind.
        if (Parallel && !Written.test(MO.getReg().id()))
          (MO.getOperandNo() == 0 ? OnlySet : OnlyCleared)
              .set(MO.getReg().id());
        else if (!Parallel) {
          OnlySet.reset(MO.getReg().id());
          OnlyCleared.reset(MO.getReg().id());
        }
        Written.set(MO.getReg().id());
        if (Unit == IA64II::UnitF)
          WrittenByF.set(MO.getReg().id());
      }
      Last = &MI;

      // A call returns to the next bundle, which starts a new group with or
      // without a stop, so record one. The rest of a flushrs's group would
      // see the register stack state from before the flush.
      if (MI.isCall() || MI.getOpcode() == IA64::FLUSHRS)
        EndGroup();
    }
    EndGroup();
  }
  return true;
}

FunctionPass *llvm::createIA64InsertStopsPass() {
  return new IA64InsertStops();
}
