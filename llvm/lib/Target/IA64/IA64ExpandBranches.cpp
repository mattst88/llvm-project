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
// Conditions tested back to back with nothing in between become one
// branch. A parallel compare writes its predicates only when its relation
// holds, so each compare after the first accumulates its result into the
// predicate the first one wrote, and a single branch tests them all.
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
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-expand-branches"
#define PASS_NAME "IA64 branch expansion"

static cl::opt<bool> RenameBranchPredicates(
    "ia64-rename-branch-predicates", cl::init(true), cl::Hidden,
    cl::desc("Give a branch predicates that the branches falling through to "
             "it do not use"));

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

namespace {

// One condition of a branch: a compare of two operands.
struct Condition {
  IA64CC::CondCode CC;
  MachineOperand LHS, RHS;
  bool FP;

  // Whether a parallel compare can test the condition, or its opposite.
  bool hasParallelCompare() const {
    std::optional<int64_t> Imm;
    if (RHS.isImm())
      Imm = RHS.getImm();
    return !FP && IA64CC::getParallelCompare(CC, Imm);
  }
};

// The conditions a branch is taken on: if any of them holds, or only if all
// of them do.
struct Chain {
  bool Any = true;
  SmallVector<Condition, 4> Conds;

  void negate() {
    Any = !Any;
    for (Condition &C : Conds)
      C.CC = IA64CC::getOpposite(C.CC);
  }
};

} // end anonymous namespace

// The conditional branch a block ends in, if it ends in one, followed by
// nothing or by an unconditional branch.
static MachineInstr *getConditionalBranch(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator Br = MBB.getFirstTerminator();
  if (Br == MBB.end() || !IA64InstrInfo::isConditionalBranch(*Br) ||
      MBB.succ_size() != 2)
    return nullptr;
  MachineBasicBlock::iterator After = next_nodbg(Br, MBB.end());
  if (After != MBB.end() && (After->getOpcode() != IA64::BRUNCOND ||
                             next_nodbg(After, MBB.end()) != MBB.end()))
    return nullptr;
  return &*Br;
}

// Where a block ending in the conditional branch Br goes when it is not
// taken.
static MachineBasicBlock *getNotTaken(MachineInstr &Br) {
  MachineBasicBlock &MBB = *Br.getParent();
  MachineBasicBlock::iterator After =
      next_nodbg(MachineBasicBlock::iterator(Br), MBB.end());
  if (After != MBB.end())
    return After->getOperand(0).getMBB();
  MachineFunction::iterator Next = std::next(MBB.getIterator());
  if (Next == MBB.getParent()->end() || !MBB.isSuccessor(&*Next))
    return nullptr;
  return &*Next;
}

static bool isEmpty(const MachineBasicBlock &MBB) {
  return MBB.getFirstNonDebugInstr() == MBB.end();
}

// The block that entering MBB leads to, past any empty blocks.
static MachineBasicBlock *skipEmpty(MachineBasicBlock *MBB) {
  SmallPtrSet<MachineBasicBlock *, 4> Seen;
  while (isEmpty(*MBB) && MBB->succ_size() == 1 && Seen.insert(MBB).second)
    MBB = *MBB->succ_begin();
  return MBB;
}

// The block that the edge from Pred to MBB leads to, past any empty blocks,
// if it and the blocks passed are entered only along that path. The empty
// blocks are added to Path.
static MachineBasicBlock *
followOnlyPath(MachineBasicBlock *Pred, MachineBasicBlock *MBB,
               SmallVectorImpl<MachineBasicBlock *> &Path) {
  for (;;) {
    if (MBB->pred_size() != 1 || *MBB->pred_begin() != Pred || MBB->isEHPad() ||
        MBB->hasAddressTaken())
      return nullptr;
    if (!isEmpty(*MBB))
      return MBB;
    if (MBB->succ_size() != 1)
      return nullptr;
    Path.push_back(MBB);
    Pred = MBB;
    MBB = *MBB->succ_begin();
  }
}

// The chain taken if both Head's and Tail's are, each negated as asked, with
// Tail's conditions tested along with Head's though they were tested only
// after Head's before. That fails if the result is not all of one kind, if
// more than one of its conditions has no parallel compare, or if Tail has
// an FP compare, which may raise an exception where none was before.
static std::optional<Chain> combine(bool Any, Chain Head, bool NegateHead,
                                    Chain Tail, bool NegateTail) {
  if (NegateHead)
    Head.negate();
  if (NegateTail)
    Tail.negate();
  if ((Head.Conds.size() > 1 && Head.Any != Any) ||
      (Tail.Conds.size() > 1 && Tail.Any != Any) ||
      any_of(Tail.Conds, [](const Condition &C) { return C.FP; }))
    return std::nullopt;
  Chain Result;
  Result.Any = Any;
  append_range(Result.Conds, Head.Conds);
  append_range(Result.Conds, Tail.Conds);
  if (count_if(Result.Conds,
               [](const Condition &C) { return !C.hasParallelCompare(); }) > 1)
    return std::nullopt;
  // The compares are now in one block, so a register may be read after a
  // compare that killed it.
  for (Condition &C : Result.Conds)
    for (MachineOperand *MO : {&C.LHS, &C.RHS})
      if (MO->isReg())
        MO->setIsKill(false);
  return Result;
}

// A block ending in a conditional branch that leads to a block consisting
// only of another conditional branch, with no other way in, tests two
// conditions where one branch would do. The first block's branch is made
// to test both, and the second block's is dropped:
//
// - If the second block is where the first goes when not taken, and both
//   branches go to the same place, the branch is taken if either condition
//   holds.
// - If instead the second block goes where the first branch did when not
//   taken, the first branch goes where the second one did, if the first
//   condition does not hold and the second does.
// - If the second block is where the first branch goes, which then goes
//   where the second does on one of its conditions, the first branch goes
//   where the second does on the other, if the first condition holds and
//   the second has that result. The second block is removed.
//
// Empty blocks along the way are passed through.
static bool mergeBranches(MachineBasicBlock &MBB,
                          DenseMap<MachineBasicBlock *, Chain> &Chains,
                          const MachineBranchProbabilityInfo &MBPI) {
  MachineInstr *Br = getConditionalBranch(MBB);
  if (!Br || !Chains.count(&MBB))
    return false;
  MachineBasicBlock *Taken = Br->getOperand(0).getMBB();
  MachineBasicBlock *NotTaken = getNotTaken(*Br);
  if (!NotTaken || Taken == NotTaken)
    return false;

  for (bool Fallthrough : {true, false}) {
    SmallVector<MachineBasicBlock *, 4> Path;
    MachineBasicBlock *Next =
        followOnlyPath(&MBB, Fallthrough ? NotTaken : Taken, Path);
    if (!Next || Next == &MBB || !Chains.count(Next))
      continue;
    MachineInstr *NextBr = getConditionalBranch(*Next);
    if (!NextBr || NextBr != &*Next->getFirstNonDebugInstr())
      continue;
    MachineBasicBlock *NextTaken = NextBr->getOperand(0).getMBB();
    MachineBasicBlock *NextNotTaken = getNotTaken(*NextBr);
    if (!NextNotTaken)
      continue;

    // Where each branch leads. None may be back into the blocks merged.
    MachineBasicBlock *Y = skipEmpty(NextTaken);
    MachineBasicBlock *Z = skipEmpty(NextNotTaken);
    MachineBasicBlock *Other = skipEmpty(Fallthrough ? Taken : NotTaken);
    SmallPtrSet<MachineBasicBlock *, 8> Merged(Path.begin(), Path.end());
    Merged.insert(&MBB);
    Merged.insert(Next);
    if (Y == Z || Merged.count(Y) || Merged.count(Z) || Merged.count(Other))
      continue;

    BranchProbability ToNext =
        MBPI.getEdgeProbability(&MBB, Fallthrough ? NotTaken : Taken);
    const Chain &Head = Chains[&MBB], &Tail = Chains[Next];
    std::optional<Chain> Result;
    MachineBasicBlock *NewTaken;
    BranchProbability P;
    if (Fallthrough && Y == Other) {
      Result = combine(true, Head, false, Tail, false);
      NewTaken = Taken;
      P = MBPI.getEdgeProbability(&MBB, Taken) +
          ToNext * MBPI.getEdgeProbability(Next, NextTaken);
    } else if (Fallthrough && Z == Other) {
      Result = combine(false, Head, true, Tail, false);
      NewTaken = NextTaken;
      P = ToNext * MBPI.getEdgeProbability(Next, NextTaken);
    } else if (!Fallthrough && Y == Other) {
      Result = combine(false, Head, false, Tail, true);
      NewTaken = NextNotTaken;
      P = ToNext * MBPI.getEdgeProbability(Next, NextNotTaken);
    } else if (!Fallthrough && Z == Other) {
      Result = combine(false, Head, false, Tail, false);
      NewTaken = NextTaken;
      P = ToNext * MBPI.getEdgeProbability(Next, NextTaken);
    } else {
      continue;
    }
    if (!Result)
      continue;
    Chains[&MBB] = std::move(*Result);
    Chains.erase(Next);

    if (NewTaken != Taken) {
      MBB.removeSuccessor(Taken);
      MBB.addSuccessor(NewTaken, P);
      Br->getOperand(0).setMBB(NewTaken);
    } else {
      MBB.setSuccProbability(find(MBB.successors(), NewTaken), P);
    }
    MBB.setSuccProbability(find(MBB.successors(), NotTaken), P.getCompl());

    if (Fallthrough) {
      // The second block now goes only where its branch did not.
      NextBr->eraseFromParent();
      Next->removeSuccessor(NextTaken);
      Next->normalizeSuccProbs();
    } else {
      Path.push_back(Next);
      for (MachineBasicBlock *Dead : Path) {
        while (!Dead->succ_empty())
          Dead->removeSuccessor(Dead->succ_begin());
        Dead->eraseFromParent();
      }
    }
    return true;
  }
  return false;
}

// The scratch predicate pairs a branch may test, p6 and p7 first.
static const MCPhysReg PredicatePairs[][2] = {{IA64::P6, IA64::P7},
                                              {IA64::P8, IA64::P9},
                                              {IA64::P10, IA64::P11},
                                              {IA64::P12, IA64::P13},
                                              {IA64::P14, IA64::P15}};

// Which pair the compare for the conditional branch Br is to write, by
// index into PredicatePairs. A branch that a block falls through to after
// not taking its own would otherwise test the same predicates, and its
// compare would have to wait for a new instruction group. So the pair is
// the first one that is not live at Br and that none of the few branches
// falling through to it in a row tests.
static unsigned choosePair(MachineInstr &Br,
                           const DenseMap<MachineBasicBlock *, unsigned> &Pairs,
                           const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *Br.getParent();
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  LivePhysRegs Live(TRI);
  Live.addLiveOuts(MBB);
  for (MachineInstr &MI : reverse(MBB)) {
    Live.stepBackward(MI);
    if (&MI == &Br)
      break;
  }
  unsigned Avoid = 0;
  MachineBasicBlock *B = &MBB;
  for (unsigned Depth = 0; Depth < 4 && B != &B->getParent()->front();
       ++Depth) {
    MachineBasicBlock *Prev = &*std::prev(B->getIterator());
    auto It = Pairs.find(Prev);
    if (It == Pairs.end() || !Prev->isSuccessor(B))
      break;
    Avoid |= 1u << It->second;
    B = Prev;
  }
  auto Free = [&](unsigned I) {
    return Live.available(MRI, PredicatePairs[I][0]) &&
           Live.available(MRI, PredicatePairs[I][1]);
  };
  for (unsigned I = 0; I != std::size(PredicatePairs); ++I)
    if (!(Avoid & (1u << I)) && Free(I))
      return I;
  return 0;
}

// br.cloop branches while ar.lc is nonzero, so it can only continue a loop.
// A branch that leaves the loop when the count runs out instead continues it
// with br.cloop and then branches out.
static void expandLoopBranch(MachineBasicBlock &MBB, MachineInstr &MI,
                             const IA64InstrInfo &TII,
                             const MachineBranchProbabilityInfo &MBPI) {
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock *Target = MI.getOperand(0).getMBB();
  if (!MI.getOperand(1).getImm()) {
    bool Taken =
        MBPI.getEdgeProbability(&MBB, Target) >= BranchProbability(1, 2);
    BuildMI(MBB, MI, DL, TII.get(IA64::BR_CLOOP))
        .addMBB(Target)
        .addImm(Taken ? IA64II::WhetherSPTK : IA64II::WhetherSPNT)
        .addImm(0)
        .addImm(0);
    MI.eraseFromParent();
    return;
  }
  // The loop continues to the block the branch does not go to.
  MachineBasicBlock *Loop;
  auto Next = std::next(MI.getIterator());
  if (Next != MBB.end() && Next->getOpcode() == IA64::BRUNCOND) {
    Loop = Next->getOperand(0).getMBB();
    Next->eraseFromParent();
  } else {
    Loop = &*std::next(MBB.getIterator());
  }
  bool Taken = MBPI.getEdgeProbability(&MBB, Loop) >= BranchProbability(1, 2);
  BuildMI(MBB, MI, DL, TII.get(IA64::BR_CLOOP))
      .addMBB(Loop)
      .addImm(Taken ? IA64II::WhetherSPTK : IA64II::WhetherSPNT)
      .addImm(0)
      .addImm(0);
  BuildMI(MBB, MI, DL, TII.get(IA64::BRUNCOND)).addMBB(Target);
  MI.eraseFromParent();
}

bool IA64ExpandBranches::runOnMachineFunction(MachineFunction &MF) {
  const IA64InstrInfo &TII = *MF.getSubtarget<IA64Subtarget>().getInstrInfo();
  const MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  bool Changed = false;

  DenseMap<MachineBasicBlock *, Chain> Chains;
  if (MF.getTarget().getOptLevel() != CodeGenOptLevel::None) {
    for (MachineBasicBlock &MBB : MF) {
      MachineInstr *Br = getConditionalBranch(MBB);
      if (!Br)
        continue;
      Chain &C = Chains[&MBB];
      C.Conds.push_back({IA64CC::CondCode(Br->getOperand(1).getImm()),
                         Br->getOperand(2), Br->getOperand(3),
                         Br->getOpcode() == IA64::BRCCf});
    }
    bool Merged;
    do {
      Merged = false;
      for (MachineBasicBlock &MBB : MF)
        if (mergeBranches(MBB, Chains, MBPI)) {
          Merged = Changed = true;
          break;
        }
    } while (Merged);
  }

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  DenseMap<MachineBasicBlock *, unsigned> Pairs;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB.terminators())) {
      if (MI.getOpcode() == IA64::BRLOOP) {
        expandLoopBranch(MBB, MI, TII, MBPI);
        Changed = true;
        break;
      }
      if (!IA64InstrInfo::isConditionalBranch(MI))
        continue;
      const DebugLoc &DL = MI.getDebugLoc();
      MachineBasicBlock *Target = MI.getOperand(0).getMBB();
      unsigned Pair = RenameBranchPredicates && MF.getTarget().getOptLevel() !=
                                                    CodeGenOptLevel::None
                          ? choosePair(MI, Pairs, TRI)
                          : 0;
      MachineInstr *Before = MI.getPrevNode();
      auto It = Chains.find(&MBB);
      if (It != Chains.end() && It->second.Conds.size() > 1) {
        // The one condition with no parallel compare, if there is one, is
        // tested first, by an ordinary compare. If the branch needs all of
        // them to hold, each other one clears p6 if it does not.
        const Chain &C = It->second;
        auto Plain = find_if(C.Conds, [](const Condition &C) {
          return !C.hasParallelCompare();
        });
        if (Plain == C.Conds.end())
          Plain = C.Conds.begin();
        TII.emitCompare(MBB, MI, DL, Plain->CC, Plain->LHS, Plain->RHS);
        for (auto I = C.Conds.begin(), E = C.Conds.end(); I != E; ++I) {
          if (I == Plain)
            continue;
          bool Emitted = TII.emitParallelCompare(
              MBB, MI, DL, C.Any ? I->CC : IA64CC::getOpposite(I->CC), I->LHS,
              I->RHS, C.Any ? IA64::P6 : IA64::P7, C.Any ? IA64::P7 : IA64::P6);
          assert(Emitted && "condition has no parallel compare");
          (void)Emitted;
        }
      } else {
        TII.emitCompare(MBB, MI, DL,
                        IA64CC::CondCode(MI.getOperand(1).getImm()),
                        MI.getOperand(2), MI.getOperand(3));
      }
      bool Taken =
          MBPI.getEdgeProbability(&MBB, Target) >= BranchProbability(1, 2);
      BuildMI(MBB, MI, DL, TII.get(IA64::BR_COND))
          .addMBB(Target)
          .addImm(Taken ? IA64II::WhetherDPTK : IA64II::WhetherDPNT)
          .addImm(0)
          .addImm(0)
          .addReg(IA64::P6);
      // The compares and the branch were emitted with p6 and p7.
      if (Pair != 0)
        for (MachineInstr &New :
             make_range(Before ? std::next(MachineBasicBlock::iterator(Before))
                               : MBB.begin(),
                        MachineBasicBlock::iterator(MI)))
          for (MachineOperand &MO : New.operands())
            if (MO.isReg() &&
                (MO.getReg() == IA64::P6 || MO.getReg() == IA64::P7))
              MO.setReg(PredicatePairs[Pair][MO.getReg() == IA64::P7]);
      Pairs[&MBB] = Pair;
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
