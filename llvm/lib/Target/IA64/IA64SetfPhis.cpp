//===-- IA64SetfPhis.cpp - Carry a moved significand through phis ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A multiply moves its operands to FP registers with setf.sig, six cycles
// ahead of the xma. zstd's hash loops read the next position's bytes at
// the bottom of an iteration and hash them there, then hash the same value
// again with another prime at the top of the next one:
//
//   top:    v = phi [v0, entry], [v1, latch]
//           f = setf.sig v          <- v0 and v1 are already in FRs
//           ... xma.l f, prime2 ...
//   latch:  f1 = setf.sig v1
//           ... xma.l f1, prime1 ...
//
// Instruction selection works a block at a time and moves v once more.
// This pass replaces a setf.sig of a phi with a phi of the FP values when
// every incoming value is moved by a setf.sig whose block dominates the
// edge.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-setf-phis"
#define PASS_NAME "IA64 setf.sig through phis"

STATISTIC(NumSetfsReplaced, "Number of setf.sig of a phi replaced by a phi");

namespace {

class IA64SetfPhis : public MachineFunctionPass {
public:
  static char ID;

  IA64SetfPhis() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char IA64SetfPhis::ID = 0;

INITIALIZE_PASS_BEGIN(IA64SetfPhis, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(IA64SetfPhis, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createIA64SetfPhisPass() { return new IA64SetfPhis(); }

static bool isPlainSetf(const MachineInstr &MI) {
  if (MI.getOpcode() != IA64::SETF_SIG)
    return false;
  int PredIdx = MI.findFirstPredOperandIdx();
  return PredIdx >= 0 && MI.getOperand(PredIdx).getReg() == IA64::P0 &&
         MI.getOperand(0).getReg().isVirtual() &&
         MI.getOperand(1).getReg().isVirtual();
}

bool IA64SetfPhis::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineDominatorTree &MDT =
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  // The value a register holds, through copies.
  auto Root = [&](Register Reg) {
    for (unsigned Depth = 0; Reg.isVirtual() && Depth < 8; ++Depth) {
      MachineInstr *Def = MRI.getUniqueVRegDef(Reg);
      if (!Def || !Def->isCopy() || !Def->getOperand(1).getReg().isVirtual())
        break;
      Reg = Def->getOperand(1).getReg();
    }
    return Reg;
  };

  // The setf.sigs of each value.
  DenseMap<Register, SmallVector<MachineInstr *, 2>> Setfs;
  SmallVector<MachineInstr *, 8> OfPhis;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB) {
      if (!isPlainSetf(MI))
        continue;
      Register Src = Root(MI.getOperand(1).getReg());
      Setfs[Src].push_back(&MI);
      MachineInstr *Def = MRI.getUniqueVRegDef(Src);
      if (Def && Def->isPHI())
        OfPhis.push_back(&MI);
    }

  // A setf.sig of Reg available at the end of MBB.
  auto SetfAt = [&](Register Reg, MachineBasicBlock *MBB) -> MachineInstr * {
    auto It = Setfs.find(Reg);
    if (It == Setfs.end())
      return nullptr;
    for (MachineInstr *S : It->second)
      if (MDT.dominates(S->getParent(), MBB))
        return S;
    return nullptr;
  };

  DenseMap<Register, Register> FPPhis;
  bool Changed = false;
  for (MachineInstr *MI : OfPhis) {
    Register Phi = Root(MI->getOperand(1).getReg());
    Register FP = FPPhis.lookup(Phi);
    if (!FP) {
      MachineInstr *Def = MRI.getUniqueVRegDef(Phi);
      SmallVector<std::pair<Register, MachineBasicBlock *>, 4> Incoming;
      bool All = true;
      for (unsigned I = 1, E = Def->getNumOperands(); All && I < E; I += 2) {
        Register In = Root(Def->getOperand(I).getReg());
        MachineBasicBlock *Pred = Def->getOperand(I + 1).getMBB();
        if (In == Phi) {
          Incoming.push_back({Register(), Pred});
          continue;
        }
        MachineInstr *S = SetfAt(In, Pred);
        All = S != nullptr;
        if (All)
          Incoming.push_back({S->getOperand(0).getReg(), Pred});
      }
      if (!All)
        continue;
      MachineBasicBlock *MBB = Def->getParent();
      FP = MRI.createVirtualRegister(
          MRI.getRegClass(MI->getOperand(0).getReg()));
      auto MIB = BuildMI(*MBB, MBB->begin(), Def->getDebugLoc(),
                         TII.get(IA64::PHI), FP);
      for (auto [Reg, Pred] : Incoming) {
        MIB.addReg(Reg ? Reg : FP).addMBB(Pred);
        if (Reg)
          MRI.clearKillFlags(Reg);
      }
      FPPhis[Phi] = FP;
      LLVM_DEBUG(dbgs() << "Carrying " << printReg(Phi) << " as "
                        << *MIB.getInstr());
    }
    Register Dst = MI->getOperand(0).getReg();
    MRI.replaceRegWith(Dst, FP);
    llvm::erase(Setfs[Phi], MI);
    MI->eraseFromParent();
    ++NumSetfsReplaced;
    Changed = true;
  }
  return Changed;
}
