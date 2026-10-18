//===-- IA64LoopAliasVersioning.cpp - Version loops on pointer overlap ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The schedulers keep a load after a store unless alias analysis shows that
// the two cannot overlap. In an unrolled loop that reads one array and writes
// another, each iteration then loads only after the one before it stored, and
// the iterations run one after another. The loop vectorizer would test the
// pointers at run time, but it leaves a loop that it does not vectorize to the
// unroller, which does not.
//
// This pass versions small innermost loops on the runtime checks of loop
// access analysis, as the vectorizer would, and marks the accesses of the copy
// that runs when no pointers overlap with scoped noalias metadata. The
// iterations of that copy, once unrolled, overlap in the schedulers and the
// pipeliner.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-loop-alias-versioning"

STATISTIC(NumVersioned, "Number of loops versioned on pointer overlap");

static cl::opt<unsigned>
    MaxLoopSize("ia64-alias-versioning-max-size", cl::Hidden, cl::init(40),
                cl::desc("The most instructions in a loop that is versioned "
                         "on pointer overlap"));

static cl::opt<unsigned>
    MaxChecks("ia64-alias-versioning-max-checks", cl::Hidden, cl::init(4),
              cl::desc("The most pointer overlap checks for a loop"));

/// Marks the copy that runs when pointers overlap, which is not versioned
/// again.
static constexpr char VersionedMD[] = "llvm.loop.ia64.alias.versioned";

/// Whether \p L is small enough to version and stores to memory.
static bool isCandidate(const Loop &L) {
  if (L.getNumBlocks() != 1 || findStringMetadataForLoop(&L, VersionedMD))
    return false;
  unsigned Size = 0;
  bool HasStore = false;
  for (const Instruction &I : *L.getHeader()) {
    Size += !I.isDebugOrPseudoInst();
    HasStore |= isa<StoreInst>(I);
  }
  return HasStore && Size <= MaxLoopSize;
}

static bool versionLoop(Loop &L, LoopInfo &LI, DominatorTree &DT,
                        ScalarEvolution &SE, LoopAccessInfoManager &LAIs) {
  if (!L.isLoopSimplifyForm() || !L.getExitingBlock())
    return false;
  const LoopAccessInfo &LAI = LAIs.getInfo(L);
  const RuntimePointerChecking &Checking = *LAI.getRuntimePointerChecking();
  if (!LAI.canVectorizeMemory() || !Checking.Need || LAI.hasConvergentOp() ||
      Checking.getChecks().size() > MaxChecks ||
      !LAI.getPSE().getPredicate().isAlwaysTrue())
    return false;

  // Versioning splits the checks off the preheader, which needs a
  // predecessor.
  BasicBlock *PH = L.getLoopPreheader();
  if (!PH->getSinglePredecessor() || &*PH->begin() != PH->getTerminator())
    SplitBlock(PH, PH->getTerminator(), &DT, &LI);
  if (!L.isLCSSAForm(DT))
    formLCSSA(L, DT, &LI, &SE);
  LoopVersioning LVer(LAI, Checking.getChecks(), &L, &LI, &DT, &SE);
  LVer.versionLoop();
  LVer.annotateLoopWithNoAlias();
  addStringMetadataToLoop(LVer.getNonVersionedLoop(), VersionedMD);
  ++NumVersioned;
  return true;
}

PreservedAnalyses
IA64LoopAliasVersioningPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (F.hasOptSize())
    return PreservedAnalyses::all();
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);

  // Versioning needs a preheader and dedicated exits, which the loop
  // vectorizer would add next anyway.
  SmallVector<Loop *> Candidates;
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder())
    if (L->isInnermost() && isCandidate(*L)) {
      Changed |= simplifyLoop(L, &DT, &LI, &SE, &AC, nullptr, false);
      Candidates.push_back(L);
    }
  auto &LAIs = AM.getResult<LoopAccessAnalysis>(F);
  for (Loop *L : Candidates) {
    if (!versionLoop(*L, LI, DT, SE, LAIs))
      continue;
    // Versioning changes what the analysis of other loops depends on.
    LAIs.clear();
    Changed = true;
  }
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<LoopAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
