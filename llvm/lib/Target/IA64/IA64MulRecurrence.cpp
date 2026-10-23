//===-- IA64MulRecurrence.cpp - Multiply induction variables by adding ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A loop like spectral-norm's
//
//   for (j = 0; j < n; j++)
//     s += u[j] / ((i + j) * (i + j + 1) / 2 + i + 1);
//
// multiplies two induction variables. Itanium has no integer multiply: it
// moves both operands to FP registers, multiplies there with xma and moves
// the product back, which takes three of the loop's memory and FP slots and
// a dozen cycles of latency. The product is a quadratic in j, though, which
// two additions an iteration compute as well.
//
// This pass finds, in a loop of one block, a multiply whose value scalar
// evolution gives as {A,+,B,+,C} and replaces it with a phi of its value
// and a phi of its step from one iteration to the next. The arithmetic wraps
// the same way the multiply does.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "ia64-mul-recurrence"

STATISTIC(NumReplaced, "Number of multiplies replaced by a recurrence");

static bool replaceMuls(Loop &L, LoopInfo &LI, DominatorTree &DT,
                        ScalarEvolution &SE) {
  BasicBlock *Header = L.getHeader();
  if (!L.isInnermost() || L.getNumBlocks() != 1 ||
      L.getLoopLatch() != Header || !L.getLoopPredecessor())
    return false;

  SCEVExpander Expander(SE, "ia64.mulrec");
  SmallVector<std::pair<Instruction *, const SCEVAddRecExpr *>, 4> Muls;
  for (Instruction &I : *Header) {
    // A multiply by a constant is a few shifts and adds already.
    if (!I.getType()->isIntegerTy() ||
        !match(&I, m_Mul(m_Value(), m_Value())) ||
        isa<Constant>(I.getOperand(0)) || isa<Constant>(I.getOperand(1)))
      continue;
    auto *AR = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(&I));
    if (AR && AR->getLoop() == &L && AR->getNumOperands() == 3 &&
        isa<SCEVConstant>(AR->getOperand(2)) &&
        SE.isLoopInvariant(AR->getOperand(1), &L))
      Muls.push_back({&I, AR});
  }
  if (Muls.empty())
    return false;

  // The start and step may be recurrences of the loops around, which the
  // expander computes ahead of each of them.
  bool Changed = false;
  for (Loop *P = &L; P; P = P->getParentLoop()) {
    if (P->getLoopPreheader())
      continue;
    if (!InsertPreheaderForLoop(P, &DT, &LI, nullptr, false))
      return Changed;
    Changed = true;
  }
  BasicBlock *Preheader = L.getLoopPreheader();
  llvm::erase_if(Muls, [&](auto &M) {
    return !Expander.isSafeToExpand(M.second->getStart()) ||
           !Expander.isSafeToExpand(M.second->getOperand(1));
  });
  if (Muls.empty())
    return Changed;
  Instruction *Term = Preheader->getTerminator();
  for (auto [Mul, AR] : Muls) {
    Type *Ty = Mul->getType();
    Value *Start = Expander.expandCodeFor(AR->getStart(), Ty, Term);
    Value *Step = Expander.expandCodeFor(AR->getOperand(1), Ty, Term);
    Value *Step2 = Expander.expandCodeFor(AR->getOperand(2), Ty, Term);
    IRBuilder<> B(Header->getFirstNonPHIIt());
    PHINode *Val = B.CreatePHI(Ty, 2, Mul->getName() + ".rec");
    PHINode *Diff = B.CreatePHI(Ty, 2, Mul->getName() + ".step");
    B.SetInsertPoint(Header->getTerminator());
    Value *NextVal = B.CreateAdd(Val, Diff, Mul->getName() + ".rec.next");
    Value *NextDiff = B.CreateAdd(Diff, Step2, Mul->getName() + ".step.next");
    Val->addIncoming(Start, Preheader);
    Val->addIncoming(NextVal, Header);
    Diff->addIncoming(Step, Preheader);
    Diff->addIncoming(NextDiff, Header);
    Mul->replaceAllUsesWith(Val);
    Mul->eraseFromParent();
    ++NumReplaced;
  }
  return true;
}

PreservedAnalyses IA64MulRecurrencePass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder())
    Changed |= replaceMuls(*L, LI, DT, SE);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}
