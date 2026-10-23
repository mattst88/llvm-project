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
// It also finds a load of what a store in the loop wrote one or two
// iterations before, when no other store in the loop writes those bytes, as
// SHA-1's message schedule
//
//   for (i = 16; i < 80; i++)
//     W[i] = W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16];
//
// does once unrolled by two. Each such load becomes a chain of phis of the
// stored value, which takes the round trip through memory out of the
// loop's recurrence.
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

// Each iteration of distance is a phi, which the pipeliner keeps in a
// rotating register but which is a copy in a loop it leaves alone.
static cl::opt<unsigned> MaxStoreDistance(
    "ia64-load-reuse-store-distance", cl::Hidden, cl::init(2),
    cl::desc("The most iterations a store may be before the load it feeds"));

STATISTIC(NumReused, "Number of loads replaced by one an iteration before");
STATISTIC(NumForwarded, "Number of loads replaced by an earlier store's value");

static bool reuseLoads(Loop &L, LoopInfo &LI, DominatorTree &DT,
                       ScalarEvolution &SE, AAResults &AA) {
  BasicBlock *Header = L.getHeader();
  if (!L.isInnermost() || L.getNumBlocks() != 1 ||
      L.getLoopLatch() != Header || !L.getLoopPredecessor())
    return false;

  auto AddRecOf = [&](Value *Ptr) -> const SCEVAddRecExpr * {
    auto *AR = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(Ptr));
    return AR && AR->getLoop() == &L && AR->isAffine() ? AR : nullptr;
  };

  // Each load's address as {Start,+,Step} in this loop, and each store's.
  SmallVector<std::pair<LoadInst *, const SCEVAddRecExpr *>, 8> Loads;
  DenseMap<Instruction *, const SCEVAddRecExpr *> Stores;
  SmallVector<Instruction *, 8> Writes;
  for (Instruction &I : *Header) {
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      return false;
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->isSimple())
        continue;
      if (const SCEVAddRecExpr *AR = AddRecOf(LI->getPointerOperand()))
        Loads.push_back({LI, AR});
    } else if (I.mayWriteToMemory()) {
      Writes.push_back(&I);
      auto *SI = dyn_cast<StoreInst>(&I);
      if (SI && SI->isSimple())
        if (const SCEVAddRecExpr *AR = AddRecOf(SI->getPointerOperand()))
          Stores[SI] = AR;
    }
  }

  auto Unwritten = [&](LoadInst *LI) {
    MemoryLocation Loc = MemoryLocation::getBeforeOrAfter(
        LI->getPointerOperand(), LI->getAAMetadata());
    return all_of(Writes, [&](Instruction *W) {
      return isNoModRef(AA.getModRefInfo(W, Loc));
    });
  };

  // The store whose value a load reads Dist iterations later, when every
  // other write in the loop is a store that, in any iteration, writes other
  // bytes than the load reads.
  const DataLayout &DL = Header->getDataLayout();
  unsigned TripCount = SE.getSmallConstantTripCount(&L);
  auto ForwardingStore = [&](LoadInst *LI, const SCEVAddRecExpr *LAR,
                             unsigned &Dist) -> StoreInst * {
    auto *StepC = dyn_cast<SCEVConstant>(LAR->getStepRecurrence(SE));
    uint64_t Size = DL.getTypeStoreSize(LI->getType());
    if (!StepC || StepC->getAPInt().isNegative() ||
        StepC->getAPInt().ult(Size) || !StepC->getAPInt().isSignedIntN(32))
      return nullptr;
    int64_t Step = StepC->getAPInt().getSExtValue();
    StoreInst *Src = nullptr;
    for (Instruction *W : Writes) {
      const SCEVAddRecExpr *SAR = Stores.lookup(W);
      if (!SAR || SAR->getStepRecurrence(SE) != StepC)
        return nullptr;
      auto *Off = dyn_cast<SCEVConstant>(SE.getMinusSCEV(SAR, LAR));
      if (!Off || !Off->getAPInt().isSignedIntN(32))
        return nullptr;
      auto *SI = cast<StoreInst>(W);
      int64_t C = Off->getAPInt().getSExtValue();
      int64_t R = (C % Step + Step) % Step;
      if (!Src && R == 0 && C > 0 && C / Step <= MaxStoreDistance &&
          (C / Step == 1 || C / Step <= TripCount) &&
          SI->getValueOperand()->getType() == LI->getType()) {
        Src = SI;
        Dist = C / Step;
        continue;
      }
      uint64_t StSize = DL.getTypeStoreSize(SI->getValueOperand()->getType());
      if (uint64_t(R) < Size || R + StSize > uint64_t(Step))
        return nullptr;
    }
    return Src;
  };

  // Each load to replace, the value it takes, and how many iterations
  // before.
  struct Reuse {
    LoadInst *Later;
    Value *Earlier;
    unsigned Dist;
  };
  SmallVector<Reuse, 4> Pairs;
  for (auto &[Later, LaterAR] : Loads) {
    const SCEV *Step = LaterAR->getStepRecurrence(SE);
    bool Found = false;
    for (auto &[Earlier, EarlierAR] : Loads) {
      if (Earlier == Later || Earlier->getType() != Later->getType() ||
          EarlierAR->getStepRecurrence(SE) != Step ||
          SE.getMinusSCEV(EarlierAR, LaterAR) != Step)
        continue;
      if (Unwritten(Later) && Unwritten(Earlier)) {
        Pairs.push_back({Later, Earlier, 1});
        Found = true;
      }
      break;
    }
    unsigned Dist;
    if (!Found)
      if (StoreInst *SI = ForwardingStore(Later, LaterAR, Dist)) {
        Pairs.push_back({Later, SI->getValueOperand(), Dist});
      }
  }
  // A phi of another such phi gives the pipeliner a recurrence it
  // schedules the whole loop around, so a load that another takes the value
  // of keeps its own.
  SmallPtrSet<Value *, 4> Replaced;
  for (const Reuse &P : Pairs)
    Replaced.insert(P.Later);
  llvm::erase_if(Pairs, [&](const Reuse &P) {
    return isa<LoadInst>(P.Earlier) && Replaced.count(P.Earlier);
  });
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
  // The phis of a load Dist iterations on carry what iterations 0 to Dist - 1
  // read in from loads ahead of the loop, the first phi taking the last.
  SCEVExpander Expander(SE, "ia64.reuse");
  IRBuilder<> B(Preheader->getTerminator());
  SmallVector<SmallVector<PHINode *, 2>, 4> Phis;
  for (const Reuse &P : Pairs) {
    LoadInst *Later = P.Later;
    const auto *AR = cast<SCEVAddRecExpr>(
        SE.getSCEV(Later->getPointerOperand()));
    SmallVector<PHINode *, 2> &Chain = Phis.emplace_back();
    for (unsigned I = 0; I != P.Dist; ++I) {
      const SCEV *Addr = SE.getAddExpr(
          AR->getStart(),
          SE.getMulExpr(SE.getConstant(AR->getType(), P.Dist - 1 - I),
                        AR->getStepRecurrence(SE)));
      Value *Ptr = Expander.expandCodeFor(Addr, Later->getPointerOperandType(),
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
      Chain.push_back(Phi);
    }
  }
  for (auto [P, Chain] : zip(Pairs, Phis))
    for (unsigned I = 0; I != P.Dist; ++I)
      Chain[I]->addIncoming(I ? Chain[I - 1] : P.Earlier, Header);
  for (auto [P, Chain] : zip(Pairs, Phis)) {
    P.Later->replaceAllUsesWith(Chain.back());
    P.Later->eraseFromParent();
    if (isa<LoadInst>(P.Earlier))
      ++NumReused;
    else
      ++NumForwarded;
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
