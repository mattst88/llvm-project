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

bool IA64InsertStops::runOnMachineFunction(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Registers written in the current group, and those written by an F-unit
  // instruction.
  BitVector Written(TRI.getNumRegs());
  BitVector WrittenByF(TRI.getNumRegs());

  for (MachineBasicBlock &MBB : MF) {
    Written.reset();
    WrittenByF.reset();
    MachineInstr *Last = nullptr;
    // Whether the current group contains an alloc. No register models the
    // current frame marker, which calls and returns replace.
    bool NewFrame = false;

    auto EndGroup = [&] {
      if (Last)
        Last->setAsmPrinterFlag(IA64::StopAfter);
      Written.reset();
      WrittenByF.reset();
      NewFrame = false;
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

      if ((NewFrame && (MI.isCall() || MI.isReturn())) ||
          any_of(MI.operands(), [&](const MachineOperand &MO) {
            return Touches(MO) && Written.test(MO.getReg().id());
          }))
        EndGroup();
      NewFrame |= MI.getOpcode() == IA64::ALLOC;

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef() || !Touches(MO))
          continue;
        Written.set(MO.getReg().id());
        if (Unit == IA64II::UnitF)
          WrittenByF.set(MO.getReg().id());
      }
      Last = &MI;

      // A call returns to the next bundle, which starts a group whether
      // there is a stop or not, so say so.
      if (MI.isCall())
        EndGroup();
    }
    EndGroup();
  }
  return true;
}

FunctionPass *llvm::createIA64InsertStopsPass() {
  return new IA64InsertStops();
}
