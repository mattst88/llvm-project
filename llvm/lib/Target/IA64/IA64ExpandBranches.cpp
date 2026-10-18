//===-- IA64ExpandBranches.cpp - Split branches from their compares -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A conditional branch is a compare that writes a predicate plus a branch
// predicated on it. They stay one instruction through block placement, since
// those passes analyze and reverse branch conditions, and this pass splits
// them afterwards.
//
// The branch's whether hint (dptk/dpnt) comes from the branch probability.
//
// Two conditions tested back to back with nothing in between become one
// branch. A parallel compare writes its predicates only when its relation
// holds, so the second compare accumulates its result into the predicate
// the first one wrote, and a single branch tests both.
//
// The pass also keeps a function from ending in a call. A call returns to
// the following bundle, and an unwinder attributes that address to the
// calling function, which would be wrong if the next function started
// there. A noreturn call may be the last instruction, so an unreachable
// break.f is appended after it.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-expand-branches"
#define PASS_NAME "IA64 branch expansion"

namespace {

class IA64ExpandBranches : public MachineFunctionPass {
public:
  static char ID;

  IA64ExpandBranches() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // end anonymous namespace

char IA64ExpandBranches::ID = 0;

INITIALIZE_PASS_BEGIN(IA64ExpandBranches, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(IA64ExpandBranches, DEBUG_TYPE, PASS_NAME, false, false)

// Whether the compare of a conditional branch, with condition CC, has a
// parallel form.
static bool hasParallelCompare(const MachineInstr &Branch,
                               IA64CC::CondCode CC) {
  const MachineOperand &RHS = Branch.getOperand(3);
  std::optional<int64_t> Imm;
  if (RHS.isImm())
    Imm = RHS.getImm();
  return Branch.getOpcode() != IA64::BRCCf &&
         IA64CC::getParallelCompare(CC, Imm);
}

// A block ending in a conditional branch that falls through to a block
// consisting only of another conditional branch, with no other predecessors,
// tests two conditions where one branch would do. If both branches go to the
// same target, the merged branch is taken if either condition holds. If the
// second block's fallthrough goes to the first branch's target, the merged
// branch is the second one, taken if the first condition is false and the
// second is true.
static bool mergeBranches(MachineBasicBlock &MBB, const IA64InstrInfo &TII,
                          const MachineBranchProbabilityInfo &MBPI) {
  MachineBasicBlock::iterator First = MBB.getFirstTerminator();
  if (First == MBB.end() || !IA64InstrInfo::isConditionalBranch(*First) ||
      std::next(First) != MBB.end() || MBB.succ_size() != 2)
    return false;
  MachineBasicBlock *Target = First->getOperand(0).getMBB();
  MachineFunction::iterator NextIt = std::next(MBB.getIterator());
  if (NextIt == MBB.getParent()->end())
    return false;
  MachineBasicBlock &Next = *NextIt;
  if (&Next == Target || !MBB.isSuccessor(&Next) || Next.pred_size() != 1 ||
      Next.succ_size() != 2 || Next.hasAddressTaken() || Next.isEHPad())
    return false;

  MachineBasicBlock::iterator Second = Next.getFirstNonDebugInstr();
  // An FP compare may raise an exception, which it must not do on a path
  // the first branch would have bypassed.
  if (Second == Next.end() || !IA64InstrInfo::isConditionalBranch(*Second) ||
      Second->getOpcode() == IA64::BRCCf)
    return false;
  MachineBasicBlock *NextTarget = Second->getOperand(0).getMBB();
  // Where the second block goes when its branch is not taken.
  MachineBasicBlock *Rest = nullptr;
  MachineBasicBlock::iterator After = next_nodbg(Second, Next.end());
  if (After == Next.end()) {
    MachineFunction::iterator Fallthrough = std::next(NextIt);
    if (Fallthrough == MBB.getParent()->end())
      return false;
    Rest = &*Fallthrough;
  } else if (After->getOpcode() == IA64::BRUNCOND &&
             next_nodbg(After, Next.end()) == Next.end()) {
    Rest = After->getOperand(0).getMBB();
  } else {
    return false;
  }
  if (Rest == NextTarget || !Next.isSuccessor(Rest) ||
      !Next.isSuccessor(NextTarget) || NextTarget == &Next)
    return false;

  bool Either = NextTarget == Target;
  if (!Either && Rest != Target)
    return false;

  // The first compare is a normal one, writing the predicate and its
  // complement, and the second is the parallel one. Either condition can go
  // first, since neither compare does anything but write predicates. For
  // the branch to be taken only if both conditions hold, the second compare
  // sets the complement predicate when its condition is false.
  IA64CC::CondCode CC1 = IA64CC::CondCode(First->getOperand(1).getImm());
  IA64CC::CondCode CC2 = IA64CC::CondCode(Second->getOperand(1).getImm());
  if (!Either)
    CC1 = IA64CC::getOpposite(CC1);
  MachineInstr *Plain = &*First, *Parallel = &*Second;
  IA64CC::CondCode PlainCC = CC1, ParallelCC = CC2;
  if (!Either)
    ParallelCC = IA64CC::getOpposite(ParallelCC);
  if (!hasParallelCompare(*Parallel, ParallelCC)) {
    std::swap(Plain, Parallel);
    std::swap(PlainCC, ParallelCC);
    if (!Either) {
      PlainCC = IA64CC::getOpposite(PlainCC);
      ParallelCC = IA64CC::getOpposite(ParallelCC);
    }
    if (!hasParallelCompare(*Parallel, ParallelCC))
      return false;
  }

  BranchProbability ToNext = MBPI.getEdgeProbability(&MBB, &Next);
  BranchProbability Taken = ToNext * MBPI.getEdgeProbability(&Next, NextTarget);
  if (Either)
    Taken = MBPI.getEdgeProbability(&MBB, Target) + Taken;

  DebugLoc DL = First->getDebugLoc();
  TII.emitCompare(MBB, MBB.end(), DL, PlainCC, Plain->getOperand(2),
                  Plain->getOperand(3));
  TII.emitParallelCompare(MBB, MBB.end(), DL, ParallelCC,
                          Parallel->getOperand(2), Parallel->getOperand(3),
                          Either ? IA64::P6 : IA64::P7,
                          Either ? IA64::P7 : IA64::P6);
  BuildMI(MBB, MBB.end(), DL, TII.get(IA64::BR_COND))
      .addMBB(NextTarget)
      .addImm(Taken >= BranchProbability(1, 2) ? IA64II::WhetherDPTK
                                               : IA64II::WhetherDPNT)
      .addImm(0)
      .addImm(0)
      .addReg(IA64::P6);
  First->eraseFromParent();
  Second->eraseFromParent();

  if (Either) {
    MBB.setSuccProbability(find(MBB.successors(), Target), Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
  } else {
    MBB.removeSuccessor(Target);
    MBB.addSuccessor(NextTarget, Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
  }
  Next.removeSuccessor(NextTarget);
  Next.normalizeSuccProbs();

  // Further blocks that branch to the same target on their own conditions
  // are folded into the branch, each as another parallel compare.
  MachineBasicBlock *Empty = &Next;
  while (Either && Empty->getFirstNonDebugInstr() == Empty->end()) {
    MachineFunction::iterator MoreIt = std::next(Empty->getIterator());
    if (MoreIt == MBB.getParent()->end())
      break;
    MachineBasicBlock &More = *MoreIt;
    if (!Empty->isSuccessor(&More) || More.pred_size() != 1 ||
        More.succ_size() != 2 || More.hasAddressTaken() || More.isEHPad() ||
        &More == Target || !More.isSuccessor(Target))
      break;
    MachineBasicBlock::iterator Branch = More.getFirstNonDebugInstr();
    if (Branch == More.end() || !IA64InstrInfo::isConditionalBranch(*Branch) ||
        Branch->getOperand(0).getMBB() != Target)
      break;
    // Any remaining branch must go somewhere other than the target.
    MachineBasicBlock::iterator Tail = next_nodbg(Branch, More.end());
    if (Tail != More.end() && (Tail->getOpcode() != IA64::BRUNCOND ||
                               Tail->getOperand(0).getMBB() == Target ||
                               next_nodbg(Tail, More.end()) != More.end()))
      break;
    IA64CC::CondCode CC = IA64CC::CondCode(Branch->getOperand(1).getImm());
    if (!hasParallelCompare(*Branch, CC))
      break;
    MachineBasicBlock::iterator Merged = MBB.getFirstTerminator();
    TII.emitParallelCompare(MBB, Merged, DL, CC, Branch->getOperand(2),
                            Branch->getOperand(3), IA64::P6, IA64::P7);
    Taken += Taken.getCompl() * MBPI.getEdgeProbability(&More, Target);
    MBB.setSuccProbability(find(MBB.successors(), Target), Taken);
    MBB.setSuccProbability(find(MBB.successors(), &Next), Taken.getCompl());
    Merged->getOperand(1).setImm(Taken >= BranchProbability(1, 2)
                                     ? IA64II::WhetherDPTK
                                     : IA64II::WhetherDPNT);
    Branch->eraseFromParent();
    More.removeSuccessor(Target);
    More.normalizeSuccProbs();
    Empty = &More;
  }
  return true;
}

bool IA64ExpandBranches::runOnMachineFunction(MachineFunction &MF) {
  const IA64InstrInfo &TII = *MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  const MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  bool Changed = false;

  if (MF.getTarget().getOptLevel() != CodeGenOptLevel::None)
    for (MachineBasicBlock &MBB : MF)
      Changed |= mergeBranches(MBB, TII, MBPI);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB.terminators())) {
      if (!IA64InstrInfo::isConditionalBranch(MI))
        continue;
      const DebugLoc &DL = MI.getDebugLoc();
      MachineBasicBlock *Target = MI.getOperand(0).getMBB();
      TII.emitCompare(MBB, MI, DL, IA64CC::CondCode(MI.getOperand(1).getImm()),
                      MI.getOperand(2), MI.getOperand(3));
      bool Taken =
          MBPI.getEdgeProbability(&MBB, Target) >= BranchProbability(1, 2);
      BuildMI(MBB, MI, DL, TII.get(IA64::BR_COND))
          .addMBB(Target)
          .addImm(Taken ? IA64II::WhetherDPTK : IA64II::WhetherDPNT)
          .addImm(0)
          .addImm(0)
          .addReg(IA64::P6);
      MI.eraseFromParent();
      Changed = true;
    }
  }

  for (MachineBasicBlock &MBB : reverse(MF)) {
    auto Last = MBB.getLastNonDebugInstr();
    while (Last != MBB.end() && Last->isMetaInstruction())
      Last = Last == MBB.begin() ? MBB.end() : prev_nodbg(Last, MBB.begin());
    if (Last == MBB.end())
      continue;
    if (Last->isCall() && !Last->isReturn()) {
      BuildMI(MF.back(), MF.back().end(), DebugLoc(), TII.get(IA64::BREAK_F))
          .addImm(0)
          .addReg(IA64::P0);
      Changed = true;
    }
    break;
  }
  return Changed;
}

FunctionPass *llvm::createIA64ExpandBranchesPass() {
  return new IA64ExpandBranches();
}
