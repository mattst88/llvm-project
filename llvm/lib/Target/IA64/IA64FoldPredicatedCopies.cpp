//===-- IA64FoldPredicatedCopies.cpp - Fold select copies into users -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A select becomes a compare and a copy under one of the predicates it
// writes, "(q) mov d = t", into the register d that already holds the other
// value. When the select's only use is an ALU instruction "x = f(d)", that
// instruction waits a group for the copy. This pass gives it the complement
// c of q instead and adds a copy of it under q that reads t in place of d:
// "(c) x = f(d)" and "(q) x = f(t)". Exactly one of them takes effect, and
// they can share a group, both right after the compare.
//
// The pass runs after anti-dependence breaking, which frees t where the
// register allocator reused it before the select's use.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64ExclusivePredicates.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-fold-predicated-copies"
#define PASS_NAME "IA64 predicated copy folding"

STATISTIC(NumCopiesFolded, "Number of predicated copies folded into a user");

namespace {

class IA64FoldPredicatedCopies : public MachineFunctionPass {
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

public:
  static char ID;

  IA64FoldPredicatedCopies() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  bool foldCopy(MachineInstr &Copy) const;
  void foldInto(MachineInstr &Copy, MachineInstr &User, Register Q, Register C,
                bool ResultLive, ArrayRef<MachineInstr *> DebugUsers) const;
};

} // end anonymous namespace

char IA64FoldPredicatedCopies::ID = 0;

INITIALIZE_PASS(IA64FoldPredicatedCopies, DEBUG_TYPE, PASS_NAME, false, false)

// Whether MI is a copy, a predicated "adds r1 = 0, r3".
static bool isPredicatedCopy(const MachineInstr &MI) {
  return MI.getOpcode() == IA64::ADDS && MI.getOperand(1).isImm() &&
         MI.getOperand(1).getImm() == 0 &&
         MI.getOperand(3).getReg() != IA64::P0;
}

// Whether MI is an ALU instruction that can take any qualifying predicate.
static bool isFoldableUser(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case IA64::ADD:
  case IA64::SUB:
  case IA64::ADDS:
  case IA64::ADDP4:
  case IA64::SHLADD:
  case IA64::SHLADDP4:
  case IA64::AND:
  case IA64::ANDCM:
  case IA64::OR:
  case IA64::XOR:
  case IA64::SXT1:
  case IA64::SXT2:
  case IA64::SXT4:
  case IA64::ZXT1:
  case IA64::ZXT2:
  case IA64::ZXT4:
    break;
  }
  return MI.getOperand(MI.findFirstPredOperandIdx()).getReg() == IA64::P0 &&
         MI.getNumOperands() == MI.getNumExplicitOperands();
}

// A predicated copy "(q) mov d = t" from a select, whose result is read
// once, by an ALU instruction "u: x = f(d)" at which d dies, adds a group of
// latency to u. When q was written by a compare along with its complement
// c, replace both with "(c) x = f(d)" and "(q) x = f(t)", which can share a
// group.
bool IA64FoldPredicatedCopies::foldCopy(MachineInstr &Copy) const {
  MachineBasicBlock &MBB = *Copy.getParent();
  Register D = Copy.getOperand(0).getReg(), T = Copy.getOperand(2).getReg();
  Register Q = Copy.getOperand(3).getReg();
  if (D == T)
    return false;

  // The compare that wrote q, and so c, before the copy, with c unchanged
  // since.
  MachineInstr *Compare = nullptr;
  for (MachineInstr &MI : make_range(
           std::next(MachineBasicBlock::reverse_iterator(Copy)), MBB.rend())) {
    if (MI.isCall() || MI.isInlineAsm())
      return false;
    if (MI.modifiesRegister(Q, TRI)) {
      Compare = &MI;
      break;
    }
  }
  if (!Compare || !IA64ExclusivePredicates::writesComplement(*Compare))
    return false;
  Register P1 = Compare->getOperand(0).getReg();
  Register P2 = Compare->getOperand(1).getReg();
  if (P1 == IA64::P0 || P2 == IA64::P0 || P1 == P2)
    return false;
  Register C = P1 == Q ? P2 : P1;
  for (MachineInstr &MI :
       make_range(std::next(MachineBasicBlock::iterator(Compare)),
                  MachineBasicBlock::iterator(Copy)))
    if (MI.modifiesRegister(C, TRI))
      return false;

  // The one reader of d, with d, t, q and c unchanged until it.
  MachineInstr *User = nullptr;
  SmallVector<MachineInstr *, 2> DebugUsers;
  for (MachineInstr &MI :
       make_range(std::next(MachineBasicBlock::iterator(Copy)), MBB.end())) {
    if (MI.isDebugInstr()) {
      if (MI.isDebugValue() && MI.hasDebugOperandForReg(D))
        DebugUsers.push_back(&MI);
      continue;
    }
    if (MI.readsRegister(D, TRI)) {
      User = &MI;
      break;
    }
    if (MI.isCall() || MI.isInlineAsm() || MI.modifiesRegister(D, TRI) ||
        MI.modifiesRegister(T, TRI) || MI.modifiesRegister(Q, TRI) ||
        MI.modifiesRegister(C, TRI))
      return false;
  }
  if (!User || !isFoldableUser(*User))
    return false;

  // d must die at the user, or be written by it.
  LivePhysRegs Live(*TRI);
  Live.addLiveOuts(MBB);
  for (MachineInstr &MI : reverse(MBB)) {
    if (&MI == User)
      break;
    Live.stepBackward(MI);
  }
  if (!User->modifiesRegister(D, TRI) &&
      !Live.available(MBB.getParent()->getRegInfo(), D))
    return false;
  Live.stepBackward(*User);
  foldInto(Copy, *User, Q, C, Live.contains(User->getOperand(0).getReg()),
           DebugUsers);
  return true;
}

// Replace Copy and its User with the User under C and a copy of it under Q
// that reads Copy's source. The copy goes first if the User overwrites the
// source, so that the source's value still reaches it. ResultLive is whether
// the User's result register is live before it.
void IA64FoldPredicatedCopies::foldInto(
    MachineInstr &Copy, MachineInstr &User, Register Q, Register C,
    bool ResultLive, ArrayRef<MachineInstr *> DebugUsers) const {
  MachineBasicBlock &MBB = *Copy.getParent();
  MachineFunction &MF = *MBB.getParent();
  Register D = Copy.getOperand(0).getReg(), T = Copy.getOperand(2).getReg();
  MachineInstr *Other = MF.CloneMachineInstr(&User);
  bool OtherFirst = User.modifiesRegister(T, TRI);
  if (OtherFirst)
    MBB.insert(MachineBasicBlock::iterator(User), Other);
  else
    MBB.insertAfter(MachineBasicBlock::iterator(User), Other);
  User.getOperand(User.findFirstPredOperandIdx()).setReg(C);
  Other->getOperand(Other->findFirstPredOperandIdx()).setReg(Q);
  for (MachineOperand &MO : Other->operands())
    if (MO.isReg() && MO.isUse() && MO.getReg() == D)
      MO.setReg(T);
  // The second still reads what the first read.
  for (MachineOperand &MO : (OtherFirst ? Other : &User)->operands())
    if (MO.isReg() && MO.isUse())
      MO.setIsKill(false);
  // Nothing up to the second kills the source any more.
  for (MachineInstr &MI :
       make_range(MachineBasicBlock::iterator(Copy), MBB.end())) {
    for (MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isUse() && MO.getReg() == T)
        MO.setIsKill(false);
    if (&MI == (OtherFirst ? &User : Other))
      break;
  }
  // Each writes the result only under its predicate, so it reads the
  // register's earlier value. The scheduler may put either first, since they
  // exclude each other, so the register must be live before both. A copy of
  // the source into itself, whose result is the source, needs no copy.
  Register X = User.getOperand(0).getReg();
  if (OtherFirst && Other->getOpcode() == IA64::ADDS &&
      Other->getOperand(1).isImm() && Other->getOperand(1).getImm() == 0 &&
      Other->getOperand(2).getReg() == X) {
    Other->eraseFromParent();
    Other = nullptr;
  }
  for (MachineInstr *MI : {&User, Other})
    if (MI)
      MI->addOperand(MachineOperand::CreateReg(X, /*isDef=*/false,
                                               /*isImp=*/true));
  // The copy of the user reads the source, which is live; otherwise an
  // implicit def starts the register's life.
  if (!ResultLive && X != T)
    BuildMI(MBB,
            MachineBasicBlock::iterator(OtherFirst && Other ? Other : &User),
            User.getDebugLoc(), TII->get(TargetOpcode::IMPLICIT_DEF), X);
  for (MachineInstr *MI : DebugUsers)
    MI->setDebugValueUndef();
  Copy.eraseFromParent();
  ++NumCopiesFolded;
}

bool IA64FoldPredicatedCopies::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : make_early_inc_range(MBB))
      if (isPredicatedCopy(MI))
        Changed |= foldCopy(MI);
  return Changed;
}

FunctionPass *llvm::createIA64FoldPredicatedCopiesPass() {
  return new IA64FoldPredicatedCopies();
}
