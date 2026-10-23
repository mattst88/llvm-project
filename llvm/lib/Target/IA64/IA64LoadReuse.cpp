//===-- IA64LoadReuse.cpp - Reuse a load from the previous iteration ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A stencil like
//
//   for (j = 1; j < n; j++)
//     ex[j] = ex[j] - 0.5 * (hz[j] - hz[j - 1]);
//
// loads hz[j - 1] in each iteration though the iteration before loaded it as
// hz[j]. On Itanium every FP load reads L2, whose banks serve one 16-byte
// chunk a cycle, so a load more is one more chance of a bank conflict.
//
// This pass finds, in a loop of one block, a load whose address is another's
// less the step both advance by each iteration, and nothing in the loop may
// store to either. It replaces the first with a phi of the second's value
// from the iteration before, and of a load ahead of the loop for the first
// iteration. The pipeliner keeps the value in a rotating register.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-load-reuse"

STATISTIC(NumReused, "Number of loads replaced by one an iteration before");

static bool reuseLoads(Loop &L, LoopInfo &LI, DominatorTree &DT,
                       ScalarEvolution &SE, AAResults &AA) {
  BasicBlock *Header = L.getHeader();
  if (!L.isInnermost() || L.getNumBlocks() != 1 ||
      L.getLoopLatch() != Header || !L.getLoopPredecessor())
    return false;

  // Each load's address as {Start,+,Step} in this loop.
  SmallVector<std::pair<LoadInst *, const SCEVAddRecExpr *>, 8> Loads;
  SmallVector<Instruction *, 8> Writes;
  for (Instruction &I : *Header) {
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      return false;
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->isSimple())
        continue;
      auto *AR = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(LI->getPointerOperand()));
      if (AR && AR->getLoop() == &L && AR->isAffine())
        Loads.push_back({LI, AR});
    } else if (I.mayWriteToMemory()) {
      Writes.push_back(&I);
    }
  }

  auto Unwritten = [&](LoadInst *LI) {
    MemoryLocation Loc = MemoryLocation::getBeforeOrAfter(
        LI->getPointerOperand(), LI->getAAMetadata());
    return all_of(Writes, [&](Instruction *W) {
      return isNoModRef(AA.getModRefInfo(W, Loc));
    });
  };

  // Each load to replace and the load it takes the value of.
  SmallVector<std::pair<LoadInst *, LoadInst *>, 4> Pairs;
  for (auto &[Later, LaterAR] : Loads) {
    const SCEV *Step = LaterAR->getStepRecurrence(SE);
    for (auto &[Earlier, EarlierAR] : Loads) {
      if (Earlier == Later || Earlier->getType() != Later->getType() ||
          EarlierAR->getStepRecurrence(SE) != Step ||
          SE.getMinusSCEV(EarlierAR, LaterAR) != Step)
        continue;
      if (Unwritten(Later) && Unwritten(Earlier))
        Pairs.push_back({Later, Earlier});
      break;
    }
  }
  // A phi of another such phi gives the pipeliner a recurrence it
  // schedules the whole loop around, so a load that another takes the value
  // of keeps its own.
  SmallPtrSet<LoadInst *, 4> Replaced;
  for (auto [Later, Earlier] : Pairs)
    Replaced.insert(Later);
  llvm::erase_if(Pairs, [&](auto &P) { return Replaced.count(P.second); });
  if (Pairs.empty())
    return false;

  // The loop runs at least once, so the first iteration's load may move
  // ahead of it, though not above a test that picks between it and another
  // version of the loop.
  BasicBlock *Preheader = L.getLoopPreheader();
  if (!Preheader)
    Preheader = InsertPreheaderForLoop(&L, &DT, &LI, nullptr, false);
  if (!Preheader)
    return false;
  SCEVExpander Expander(SE, "ia64.reuse");
  DenseMap<LoadInst *, PHINode *> Phis;
  IRBuilder<> B(Preheader->getTerminator());
  for (auto [Later, Earlier] : Pairs) {
    const auto *AR = cast<SCEVAddRecExpr>(
        SE.getSCEV(Later->getPointerOperand()));
    Value *Ptr = Expander.expandCodeFor(AR->getStart(),
                                        Later->getPointerOperandType(),
                                        Preheader->getTerminator());
    LoadInst *First = B.CreateAlignedLoad(Later->getType(), Ptr,
                                          Later->getAlign(),
                                          Later->getName() + ".first");
    First->copyMetadata(*Later, {LLVMContext::MD_tbaa,
                                 LLVMContext::MD_alias_scope,
                                 LLVMContext::MD_noalias});
    PHINode *Phi = PHINode::Create(Later->getType(), 2,
                                   Later->getName() + ".prev",
                                   Header->getFirstNonPHIIt());
    Phi->addIncoming(First, Preheader);
    Phis[Later] = Phi;
  }
  for (auto [Later, Earlier] : Pairs)
    Phis[Later]->addIncoming(Earlier, Header);
  for (auto [Later, Earlier] : Pairs) {
    Later->replaceAllUsesWith(Phis[Later]);
    Later->eraseFromParent();
    ++NumReused;
  }
  return true;
}

PreservedAnalyses IA64LoadReusePass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder())
    Changed |= reuseLoads(*L, LI, DT, SE, AA);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}
