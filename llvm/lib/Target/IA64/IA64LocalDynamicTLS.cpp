//===-- IA64LocalDynamicTLS.cpp - Reuse the local-dynamic TLS base --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A local-dynamic TLS access calls __tls_get_addr with the module ID and an
// offset of zero to get the base of the module's TLS block, then adds the
// variable's DTP-relative offset, which the linker resolves. The base is the
// same for every such variable for the whole function, so a call dominated by
// another one is removed and uses the dominating call's result.
//
// Each access is lowered to its own call, since neither instruction selector
// sees more than one access at a time. Both selectors emit this sequence:
//
//   %entry = ADDL @ltoff(@dtpmod(sym)), r1
//   %module = LD8 %entry
//   ADJCALLSTACKDOWN
//   $r120 = COPY %module
//   $r121 = COPY %zero, which is a copy of r0
//   CALL &__tls_get_addr
//   ADJCALLSTACKUP
//   %base = COPY $r8
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-local-dynamic-tls"
#define PASS_NAME "IA64 local dynamic TLS cleanup"

namespace {

class IA64LocalDynamicTLS : public MachineFunctionPass {
public:
  static char ID;

  IA64LocalDynamicTLS() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

private:
  bool visit(MachineDomTreeNode &Node, Register Base);
};

} // end anonymous namespace

char IA64LocalDynamicTLS::ID = 0;

INITIALIZE_PASS_BEGIN(IA64LocalDynamicTLS, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(IA64LocalDynamicTLS, DEBUG_TYPE, PASS_NAME, false, false)

// The instruction defining the vreg copied to \p Arg, searching from
// \p Begin up to \p Call.
static const MachineInstr *argumentDef(MachineBasicBlock::iterator Begin,
                                       MachineBasicBlock::iterator Call,
                                       MCRegister Arg,
                                       const MachineRegisterInfo &MRI) {
  const MachineInstr *Def = nullptr;
  for (const MachineInstr &MI : make_range(Begin, Call)) {
    if (!MI.definesRegister(Arg, /*TRI=*/nullptr))
      continue;
    if (Def || !MI.isCopy() || !MI.getOperand(1).getReg().isVirtual())
      return nullptr;
    Def = MRI.getVRegDef(MI.getOperand(1).getReg());
  }
  return Def;
}

// If \p Call computes the module's TLS base, return the copy of its result,
// and set \p First to the start of the call sequence, which runs from
// \p First up to that copy.
static MachineInstr *matchBaseCall(MachineInstr &Call,
                                   MachineBasicBlock::iterator &First) {
  if (Call.getOpcode() != IA64::CALL || !Call.getOperand(0).isSymbol() ||
      StringRef(Call.getOperand(0).getSymbolName()) != "__tls_get_addr")
    return nullptr;
  MachineBasicBlock &MBB = *Call.getParent();
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();

  MachineBasicBlock::iterator Down = Call.getIterator();
  do {
    if (Down == MBB.begin())
      return nullptr;
    --Down;
    if (Down->isCall())
      return nullptr;
  } while (Down->getOpcode() != TII.getCallFrameSetupOpcode());

  const MachineInstr *Module =
      argumentDef(Down, Call.getIterator(), IA64::R120, MRI);
  if (!Module || Module->getOpcode() != IA64::LD8 ||
      !Module->getOperand(1).isReg() ||
      !Module->getOperand(1).getReg().isVirtual())
    return nullptr;
  const MachineInstr *Entry = MRI.getVRegDef(Module->getOperand(1).getReg());
  if (!Entry || Entry->getOpcode() != IA64::ADDL ||
      !Entry->getOperand(1).isGlobal() ||
      Entry->getOperand(1).getTargetFlags() != IA64::S_LTOFF_DTPMOD)
    return nullptr;
  const MachineInstr *Offset =
      argumentDef(Down, Call.getIterator(), IA64::R121, MRI);
  if (!Offset || !Offset->isCopy() ||
      Offset->getOperand(1).getReg() != IA64::R0)
    return nullptr;

  // Only the argument setup may lie between the call frame setup and the
  // call, and the values it computes must have no other uses.
  for (MachineBasicBlock::iterator I = std::next(Down), E = Call.getIterator();
       I != E; ++I) {
    if (I->isDebugInstr())
      continue;
    if (!I->getOperand(0).isReg())
      return nullptr;
    Register Def = I->getOperand(0).getReg();
    if (Def == IA64::R120 || Def == IA64::R121)
      continue;
    // The copy of r0 may be shared by other calls.
    if (&*I == Offset)
      continue;
    if ((&*I != Module && &*I != Entry) || !MRI.hasOneNonDBGUse(Def))
      return nullptr;
  }

  MachineBasicBlock::iterator Up = std::next(Call.getIterator());
  if (Up == MBB.end() || Up->getOpcode() != TII.getCallFrameDestroyOpcode())
    return nullptr;
  MachineBasicBlock::iterator Result = std::next(Up);
  if (Result == MBB.end() || !Result->isCopy() ||
      Result->getOperand(1).getReg() != IA64::R8 ||
      !Result->getOperand(0).getReg().isVirtual())
    return nullptr;
  First = Down;
  return &*Result;
}

// Walk the dominator subtree of \p Node. \p Base is the result of a
// dominating base call, if any.
bool IA64LocalDynamicTLS::visit(MachineDomTreeNode &Node, Register Base) {
  MachineBasicBlock &MBB = *Node.getBlock();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I++;
    MachineBasicBlock::iterator First;
    MachineInstr *Result = matchBaseCall(MI, First);
    if (!Result)
      continue;
    Register Dst = Result->getOperand(0).getReg();
    if (!Base) {
      Base = Dst;
      continue;
    }
    // The linkage table load for the call is now dead; dead machine
    // instruction elimination removes it.
    I = std::next(Result->getIterator());
    MachineInstr *Copy =
        BuildMI(MBB, I, Result->getDebugLoc(), TII.get(TargetOpcode::COPY), Dst)
            .addReg(Base);
    MRI.clearKillFlags(Base);
    for (MachineBasicBlock::iterator D = First, E = Copy->getIterator();
         D != E;) {
      MachineInstr &Dead = *D++;
      // Keep a copy of r0 that has other uses.
      if (Dead.isCopy() && Dead.getOperand(0).getReg().isVirtual() &&
          !MRI.hasOneNonDBGUse(Dead.getOperand(0).getReg()))
        continue;
      Dead.eraseFromParent();
    }
    Changed = true;
  }
  for (MachineDomTreeNode *Child : Node.children())
    Changed |= visit(*Child, Base);
  return Changed;
}

bool IA64LocalDynamicTLS::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  auto &DT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  return visit(*DT.getRootNode(), Register());
}

FunctionPass *llvm::createIA64LocalDynamicTLSPass() {
  return new IA64LocalDynamicTLS();
}
