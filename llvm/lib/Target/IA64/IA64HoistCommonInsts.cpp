//===-- IA64HoistCommonInsts.cpp - Hoist what both arms of a branch do ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A branch whose arms compute the same value in different orders, as in
//
//   if (R->haveRange)
//     return R->left + dm1 * (double)k * R->width;
//   else
//     return dm1 * (double)k;
//
// leaves the conversion in each arm, after the branch. SimplifyCFG hoists only
// what the arms begin with in the same order. On IA-64 such a conversion
// starts a long chain of FP latency (setf, fcvt, fmpy) that could have begun
// before the branch's compare was ready.
//
// This pass hoists each instruction of one arm that the other arm computes as
// well, wherever it appears there, into the block that branches. It hoists
// only FP work and what feeds it: a one-cycle integer chain gains nothing by
// starting early, and lengthens the path to the branch, which issues after
// everything in its block.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-hoist-common-insts"

STATISTIC(NumHoisted, "Number of instructions hoisted from both arms");

/// How many instructions of each arm are looked at.
static constexpr unsigned ScanLimit = 32;

namespace {
/// An instruction of an arm, and whether everything before it in the arm
/// reaches it without writing memory.
struct ArmInst {
  Instruction *I;
  bool Clean;
};
} // namespace

/// The instructions at the start of \p BB that could move to its predecessor
/// if their operands were there.
static SmallVector<ArmInst, 16> armInsts(BasicBlock &BB) {
  SmallVector<ArmInst, 16> Insts;
  bool Clean = true;
  for (Instruction &I : BB) {
    if (Insts.size() == ScanLimit || I.isTerminator())
      break;
    if (!isa<PHINode>(I) && !isa<AllocaInst>(I) && !I.mayHaveSideEffects() &&
        (!I.mayReadFromMemory() || isa<LoadInst>(I)))
      Insts.push_back({&I, Clean});
    Clean &=
        !I.mayWriteToMemory() && isGuaranteedToTransferExecutionToSuccessor(&I);
  }
  return Insts;
}

/// Whether \p I can run in \p Pred once the instructions in \p Matched are
/// there, given whether its arm reaches it cleanly. A load must also have its
/// address ready when Pred begins, so that it overlaps Pred's work. One whose
/// address Pred computes, or another hoisted load does, would hold up the
/// branch, where the arm would have overlapped it with the arm's other work.
static bool canHoist(const ArmInst &AI, BasicBlock *Pred,
                     const SmallPtrSetImpl<Instruction *> &Matched) {
  Instruction *I = AI.I;
  if (!AI.Clean && I->mayReadFromMemory())
    return false;
  bool IsLoad = isa<LoadInst>(I);
  if (IsLoad && !cast<LoadInst>(I)->isSimple())
    return false;
  return all_of(I->operands(), [&](Value *Op) {
    auto *OpI = dyn_cast<Instruction>(Op);
    if (!OpI)
      return true;
    if (Matched.contains(OpI))
      return !IsLoad;
    return OpI->getParent() != I->getParent() &&
           (!IsLoad || OpI->getParent() != Pred);
  });
}

/// Whether \p A computes what \p B does, given the instructions of B's arm
/// matched so far with those of A's.
static bool sameValue(Instruction *A, Instruction *B,
                      const DenseMap<Instruction *, Instruction *> &Match) {
  if (!A->isSameOperationAs(B) || A->getNumOperands() != B->getNumOperands())
    return false;
  for (auto [OpA, OpB] : zip(A->operands(), B->operands())) {
    auto *BI = dyn_cast<Instruction>(OpB);
    Value *Want = BI && Match.count(BI) ? Match.lookup(BI) : OpB.get();
    if (OpA != Want)
      return false;
  }
  return true;
}

/// Whether \p I has a long latency on IA-64: FP work, or a conversion to or
/// from FP.
static bool isFPWork(const Instruction *I) {
  return I->getType()->isFPOrFPVectorTy() ||
         any_of(I->operands(), [](const Value *Op) {
           return Op->getType()->isFPOrFPVectorTy();
         });
}

static bool hoistFromArms(CondBrInst *Br) {
  BasicBlock *Pred = Br->getParent();
  BasicBlock *A = Br->getSuccessor(0), *B = Br->getSuccessor(1);
  if (A == B || A->getSinglePredecessor() != Pred ||
      B->getSinglePredecessor() != Pred)
    return false;
  SmallVector<ArmInst, 16> AInsts = armInsts(*A);
  SmallVector<ArmInst, 16> BInsts = armInsts(*B);

  // Match the arms' instructions. Matching one can make the next one's
  // operands available, so the arms are walked in order.
  SmallVector<std::pair<Instruction *, Instruction *>, 16> Pairs;
  DenseMap<Instruction *, Instruction *> Match;
  SmallPtrSet<Instruction *, 16> Matched;
  for (ArmInst &BI : BInsts) {
    if (!canHoist(BI, Pred, Matched))
      continue;
    for (ArmInst &AI : AInsts) {
      if (Matched.contains(AI.I) || !sameValue(AI.I, BI.I, Match) ||
          !canHoist(AI, Pred, Matched))
        continue;
      Pairs.push_back({AI.I, BI.I});
      Match[BI.I] = AI.I;
      Matched.insert(AI.I);
      Matched.insert(BI.I);
      break;
    }
  }

  // Keep the FP work and what feeds it. A pair's users come after it.
  SmallPtrSet<Instruction *, 16> Wanted;
  for (auto [I, _] : reverse(Pairs))
    if (isFPWork(I) || any_of(I->users(), [&](User *U) {
          return Wanted.contains(cast<Instruction>(U));
        }))
      Wanted.insert(I);

  for (auto [I, BIn] : Pairs) {
    if (!Wanted.contains(I))
      continue;
    I->moveBefore(Br->getIterator());
    I->andIRFlags(BIn);
    combineMetadataForCSE(I, BIn, /*DoesKMove=*/true);
    I->applyMergedLocation(I->getDebugLoc(), BIn->getDebugLoc());
    BIn->replaceAllUsesWith(I);
    BIn->eraseFromParent();
    ++NumHoisted;
  }
  return !Wanted.empty();
}

PreservedAnalyses IA64HoistCommonInstsPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  bool Changed = false;
  for (BasicBlock &BB : F)
    if (auto *Br = dyn_cast<CondBrInst>(BB.getTerminator()))
      Changed |= hoistFromArms(Br);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
