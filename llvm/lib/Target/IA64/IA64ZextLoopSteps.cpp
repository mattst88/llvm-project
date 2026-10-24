//===-- IA64ZextLoopSteps.cpp - Zero-extend the steps of loop counters ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A loop that counts up to a bound by a step of int, as in
//
//   for (i = 0; i < n; i += 2 * dual)
//
// widens the step with a sign extension, which leaves the sign of the step
// unknown. The pipeliner then cannot tell that the loop's accesses move away
// from those of earlier iterations, and keeps them in order.
//
// A loop whose only exit is the test of such a counter cannot repeat with a
// negative step: the counter would stay below the bound until it overflowed,
// which its nsw add makes undefined. So the step is never negative when the
// loop repeats, and when it does not, a zero extension of a negative step
// still takes the counter past the bound, if that does not overflow. This
// pass zero-extends the step instead.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "ia64-zext-loop-steps"

STATISTIC(NumZexted, "Number of loop steps zero-extended");

/// Whether every pass of \p L runs to its latch, and leaves the loop only
/// there.
static bool alwaysReachesLatch(const Loop &L) {
  BasicBlock *Latch = L.getLoopLatch();
  if (!Latch || !L.isInnermost() || L.getExitingBlock() != Latch)
    return false;
  for (BasicBlock *BB : L.blocks())
    for (Instruction &I : *BB)
      if (!isGuaranteedToTransferExecutionToSuccessor(&I))
        return false;
  return true;
}

static bool zextStep(Loop &L, ScalarEvolution &SE) {
  if (!alwaysReachesLatch(L))
    return false;
  BasicBlock *Latch = L.getLoopLatch();
  BasicBlock *Preheader = L.getLoopPreheader();
  auto *BI = dyn_cast<CondBrInst>(Latch->getTerminator());
  if (!Preheader || !BI)
    return false;

  // The loop repeats while the counter is less than a bound.
  CmpPredicate Pred;
  Value *Next, *Bound;
  if (!match(BI->getCondition(),
             m_OneUse(m_ICmp(Pred, m_Value(Next), m_Value(Bound)))))
    return false;
  if (BI->getSuccessor(0) != L.getHeader())
    Pred = CmpInst::getInversePredicate(Pred);
  if (L.isLoopInvariant(Next)) {
    std::swap(Next, Bound);
    Pred = CmpInst::getSwappedPredicate(Pred);
  }
  if ((Pred != CmpInst::ICMP_SLT && Pred != CmpInst::ICMP_SLE) ||
      !L.isLoopInvariant(Bound))
    return false;

  // The counter steps by a sign extension, and its next value is used only
  // by the test and the counter itself, as only those see a negative step
  // when the loop does not repeat.
  auto *Add = dyn_cast<BinaryOperator>(Next);
  if (!Add || Add->getOpcode() != Instruction::Add || !Add->hasNoSignedWrap() ||
      !Add->hasNUses(2))
    return false;
  PHINode *Phi = nullptr;
  Value *Step = nullptr;
  for (unsigned Idx : {0, 1}) {
    auto *P = dyn_cast<PHINode>(Add->getOperand(Idx));
    if (P && P->getParent() == L.getHeader() &&
        P->getIncomingValueForBlock(Latch) == Add) {
      Phi = P;
      Step = Add->getOperand(1 - Idx);
    }
  }
  Value *Narrow;
  if (!Phi || !match(Step, m_SExt(m_Value(Narrow))) || !L.isLoopInvariant(Step))
    return false;

  // The first pass's counter must not overflow with the zero-extended step.
  Value *Start = Phi->getIncomingValueForBlock(Preheader);
  const SCEV *ZextStep =
      SE.getZeroExtendExpr(SE.getSCEV(Narrow), Step->getType());
  if (!SE.willNotOverflow(Instruction::Add, /*Signed=*/true, SE.getSCEV(Start),
                          ZextStep))
    return false;

  auto *Zext = new ZExtInst(Narrow, Step->getType(), "step.zext",
                            Preheader->getTerminator()->getIterator());
  Add->replaceUsesOfWith(Step, Zext);
  Add->setHasNoUnsignedWrap(false);
  SE.forgetLoop(&L);
  ++NumZexted;
  return true;
}

PreservedAnalyses IA64ZextLoopStepsPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder())
    Changed |= zextStep(*L, SE);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<ScalarEvolutionAnalysis>();
  return PA;
}
