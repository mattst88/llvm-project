//===-- IA64WidenIndices.cpp - Version loops to widen 32-bit indices ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An unsigned 32-bit array index, as in a loop over i*N+j with unsigned i, j
// and N, has to be zero-extended at each access, since nothing says the sum
// does not wrap: IA-64 pays a zxt4 or addp4 before the shladd, and LSR cannot
// turn the index into a pointer that steps through the array, so a multiply
// stays in the loop for a column walk.  IndVars widens the induction
// variables it can prove do not wrap; the index built from them it cannot.
//
// Where such an index is an affine recurrence of an innermost loop, version
// the loop on the recurrence not wrapping over the loop's trip count, and in
// the loop that runs when it does not, replace the zero extension with a
// 64-bit recurrence.  The clone keeps the 32-bit arithmetic for the runs that
// wrap.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64TargetMachine.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-widen-indices"

static cl::opt<bool>
    EnableWidenIndices("ia64-enable-widen-indices", cl::init(true), cl::Hidden,
                       cl::desc("Version loops to widen 32-bit indices"));

static cl::opt<unsigned>
    MaxWidenChecks("ia64-widen-indices-max-checks", cl::init(4), cl::Hidden,
                   cl::desc("The most recurrences one loop is versioned on"));

static cl::opt<unsigned> MinWidenTrips(
    "ia64-widen-indices-min-trips", cl::init(8), cl::Hidden,
    cl::desc("The fewest iterations a loop may be known to run at most and "
             "be versioned"));

static constexpr StringLiteral WidenedTag =
    "llvm.loop.ia64.widen_indices.done";

// An upper bound on S at CtxI, from LVI's ranges of the values it is built
// from, which unlike SCEV's loop guards follow each path into a phi.
static ConstantRange getRangeAt(const SCEV *S, ScalarEvolution &SE,
                                LazyValueInfo &LVI, Instruction *CtxI) {
  unsigned Bits = SE.getTypeSizeInBits(S->getType());
  if (auto *C = dyn_cast<SCEVConstant>(S))
    return C->getAPInt();
  if (auto *U = dyn_cast<SCEVUnknown>(S))
    if (U->getType()->isIntegerTy())
      return LVI.getConstantRange(U->getValue(), CtxI, /*UndefAllowed=*/false)
          .intersectWith(SE.getUnsignedRange(S));
  if (auto *C = dyn_cast<SCEVCastExpr>(S)) {
    ConstantRange Op = getRangeAt(C->getOperand(), SE, LVI, CtxI);
    switch (C->getSCEVType()) {
    case scZeroExtend:
      return Op.zeroExtend(Bits);
    case scSignExtend:
      return Op.signExtend(Bits);
    case scTruncate:
      return Op.truncate(Bits);
    default:
      break;
    }
  }
  if (auto *A = dyn_cast<SCEVAddExpr>(S)) {
    ConstantRange R = getRangeAt(A->getOperand(0), SE, LVI, CtxI);
    for (const SCEV *Op : drop_begin(A->operands()))
      R = R.add(getRangeAt(Op, SE, LVI, CtxI));
    return R;
  }
  return SE.getUnsignedRange(S);
}

// Checks that hold for each run of an inner loop hold for every run inside a
// run of its parent P if they hold at P's first and last iterations and are
// linear in between: a check invariant in P stays, and an affine recurrence
// of P is checked at both ends instead.  With P's backedge count below 2^32
// and the step within 2^31 either way, the recurrence cannot wrap a 64-bit
// register between them.  Lifting the checks to P's preheader runs them once
// a run of P rather than once an iteration.
namespace {
// Rewrites zext({S,+,C}) of a 32-bit recurrence of P as {zext S,+,sext C},
// collecting the end values that must stay below 2^32 for that to hold over
// a run of P: the same reasoning as for the widened index, a loop out.
class ZExtRecurrenceRewriter
    : public SCEVRewriteVisitor<ZExtRecurrenceRewriter> {
  const Loop *P;
  const SCEV *BTC;
  SmallVectorImpl<const SCEV *> &Ends;

public:
  ZExtRecurrenceRewriter(ScalarEvolution &SE, const Loop *P, const SCEV *BTC,
                         SmallVectorImpl<const SCEV *> &Ends)
      : SCEVRewriteVisitor(SE), P(P), BTC(BTC), Ends(Ends) {}

  const SCEV *visitZeroExtendExpr(const SCEVZeroExtendExpr *Z) {
    auto *AR = dyn_cast<SCEVAddRecExpr>(Z->getOperand());
    Type *I64 = BTC->getType();
    if (!AR || AR->getLoop() != P || !AR->isAffine() || Z->getType() != I64 ||
        SE.getTypeSizeInBits(AR->getType()) != 32)
      return SCEVRewriteVisitor::visitZeroExtendExpr(Z);
    const SCEV *Start = SE.getZeroExtendExpr(AR->getStart(), I64);
    const SCEV *Step = SE.getSignExtendExpr(AR->getStepRecurrence(SE), I64);
    Ends.push_back(SE.getAddExpr(Start, SE.getMulExpr(Step, BTC)));
    return SE.getAddRecExpr(Start, Step, P, SCEV::FlagNone);
  }
};
} // namespace

static bool liftChecks(Loop *P, SmallSetVector<const SCEV *, 4> &Checks,
                       ScalarEvolution &SE, SCEVExpander &Exp) {
  if (findStringMetadataForLoop(P, WidenedTag) || !P->getUniqueExitBlock() ||
      !P->getExitingBlock())
    return false;
  const SCEV *BTC = SE.getSymbolicMaxBackedgeTakenCount(P);
  if (isa<SCEVCouldNotCompute>(BTC) ||
      SE.getTypeSizeInBits(BTC->getType()) > 64)
    return false;
  Type *I64 = Type::getInt64Ty(P->getHeader()->getContext());
  BTC = SE.getNoopOrZeroExtend(BTC, I64);
  const SCEV *Limit = SE.getConstant(I64, 1ULL << 32);
  ConstantRange StepRange(APInt(64, -(1LL << 31), /*isSigned=*/true),
                          APInt(64, (1LL << 31) + 1));
  SmallSetVector<const SCEV *, 4> Lifted;
  if (!SE.isKnownPredicate(ICmpInst::ICMP_ULT, BTC, Limit))
    Lifted.insert(BTC);
  SmallVector<const SCEV *, 4> Ends;
  ZExtRecurrenceRewriter Rewriter(SE, P, BTC, Ends);
  for (const SCEV *S : Checks) {
    S = Rewriter.visit(S);
    if (SE.isLoopInvariant(S, P)) {
      Lifted.insert(S);
      continue;
    }
    auto *AR = dyn_cast<SCEVAddRecExpr>(S);
    if (!AR || AR->getLoop() != P || !AR->isAffine() ||
        !StepRange.contains(SE.getSignedRange(AR->getStepRecurrence(SE))))
      return false;
    for (const SCEV *E : {static_cast<const SCEV *>(AR->getStart()),
                          AR->evaluateAtIteration(BTC, SE)})
      if (!SE.isKnownPredicate(ICmpInst::ICMP_ULT, E, Limit))
        Lifted.insert(E);
  }
  for (const SCEV *E : Ends)
    if (!SE.isKnownPredicate(ICmpInst::ICMP_ULT, E, Limit))
      Lifted.insert(E);
  Instruction *Pt = P->getLoopPreheader()->getTerminator();
  if (Lifted.size() > MaxWidenChecks || !all_of(Lifted, [&](const SCEV *S) {
        return Exp.isSafeToExpandAt(S, Pt);
      }))
    return false;
  Checks = std::move(Lifted);
  return true;
}

static bool widenLoop(Function &F, Loop *L, LoopInfo &LI, DominatorTree &DT,
                      ScalarEvolution &SE, AssumptionCache &AC,
                      LazyValueInfo &LVI) {
  if (findStringMetadataForLoop(L, WidenedTag))
    return false;
  // The 64-bit recurrence may start from one of an outer loop, which the
  // expander can only build in loop-simplify form.
  Loop *Top = L->getOutermostLoop();
  bool Changed = simplifyLoop(Top, &DT, &LI, &SE, &AC, nullptr, false);
  Changed |= formLCSSARecursively(*Top, DT, &LI, &SE);
  for (Loop *P = L; P; P = P->getParentLoop())
    if (!P->isLoopSimplifyForm())
      return Changed;
  if (!L->getUniqueExitBlock() || !L->getExitingBlock() ||
      isa<SCEVCouldNotCompute>(SE.getSymbolicMaxBackedgeTakenCount(L)))
    return Changed;
  // The check costs a few instructions and a branch on entry, and each index
  // widened saves one or two an iteration: a loop that runs only a few times
  // loses, like the tail of bzip2's move-to-front shift, after it moved four
  // bytes at a time.
  BasicBlock *CheckBB = L->getLoopPreheader();
  Instruction *CheckPt = CheckBB->getTerminator();
  const SCEV *BTC = SE.getSymbolicMaxBackedgeTakenCount(L);
  APInt MaxBTC = getRangeAt(BTC, SE, LVI, CheckPt).getUnsignedMax();
  if (MinWidenTrips && MaxBTC.ult(MinWidenTrips - 1))
    return Changed;

  // zext({S,+,C}) is {zext S,+,sext C} over iterations 0 to the backedge
  // count B if the 64-bit recurrence stays below 2^32 from the first to the
  // last, and since it is linear, it does if it starts there, as zext S does,
  // and ends there.  With B below 2^32, sext C * B cannot overflow, and an
  // end that wraps below zero is above 2^32 unsigned.  That is cheaper to
  // check than SCEV's no-wrap predicate, a multiplication with overflow per
  // recurrence.
  Type *I64 = Type::getInt64Ty(F.getContext());
  if (SE.getTypeSizeInBits(BTC->getType()) > 64)
    return Changed;
  BTC = SE.getNoopOrZeroExtend(BTC, I64);
  const SCEV *Limit = SE.getConstant(I64, 1ULL << 32);
  SCEVExpander CheckExp(SE, "widen.check");
  SmallSetVector<const SCEV *, 4> Checks;
  if (!SE.isKnownPredicate(ICmpInst::ICMP_ULT, BTC, Limit))
    Checks.insert(BTC);
  SmallVector<std::pair<ZExtInst *, const SCEVAddRecExpr *>, 4> Widen;
  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : *BB) {
      auto *ZI = dyn_cast<ZExtInst>(&I);
      if (!ZI || !ZI->getType()->isIntegerTy(64) ||
          !ZI->getSrcTy()->isIntegerTy(32) ||
          none_of(ZI->users(), IsaPred<GetElementPtrInst>))
        continue;
      // A recurrence that cannot wrap folds the extension in already.
      if (isa<SCEVAddRecExpr>(SE.getSCEV(ZI)))
        continue;
      auto *Op = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(ZI->getOperand(0)));
      if (!Op || Op->getLoop() != L || !Op->isAffine())
        continue;
      auto *AR = cast<SCEVAddRecExpr>(
          SE.getAddRecExpr(SE.getZeroExtendExpr(Op->getStart(), I64),
                           SE.getSignExtendExpr(Op->getStepRecurrence(SE), I64),
                           L, SCEV::FlagNone));
      const SCEV *End = SE.getAddExpr(
          AR->getStart(), SE.getMulExpr(AR->getStepRecurrence(SE), BTC));
      if (!SE.isKnownPredicate(ICmpInst::ICMP_ULT, End, Limit))
        Checks.insert(End);
      if (Checks.size() > MaxWidenChecks ||
          !CheckExp.isSafeToExpandAt(End, CheckPt))
        return Changed;
      Widen.push_back({ZI, AR});
    }
  }
  if (Widen.empty())
    return Changed;

  // V is the loop that is versioned: L, or the outermost loop around it that
  // the checks can be lifted to.
  Loop *V = L;
  while (V->getParentLoop() &&
         liftChecks(V->getParentLoop(), Checks, SE, CheckExp))
    V = V->getParentLoop();
  CheckBB = V->getLoopPreheader();
  CheckPt = CheckBB->getTerminator();

  IRBuilder<> B(CheckPt);
  Value *Wraps = nullptr;
  for (const SCEV *S : Checks) {
    Value *Above = B.CreateICmpUGT(CheckExp.expandCodeFor(S, I64, CheckPt),
                                   B.getInt64(0xffffffff));
    Wraps = Wraps ? B.CreateOr(Wraps, Above) : Above;
  }

  if (Wraps) {
    BasicBlock *PH = SplitBlock(CheckBB, CheckBB->getTerminator(), &DT, &LI,
                                nullptr, V->getHeader()->getName() + ".ph");
    ValueToValueMapTy VMap;
    SmallVector<BasicBlock *, 8> SlowBlocks;
    Loop *Slow = cloneLoopWithPreheader(PH, CheckBB, V, VMap, ".wraps", &LI,
                                        &DT, SlowBlocks);
    remapInstructionsInBlocks(SlowBlocks, VMap);
    Instruction *OldTerm = CheckBB->getTerminator();
    CondBrInst::Create(Wraps, Slow->getLoopPreheader(), V->getLoopPreheader(),
                       OldTerm->getIterator());
    OldTerm->eraseFromParent();

    BasicBlock *Exit = V->getExitBlock();
    DT.changeImmediateDominator(Exit, CheckBB);
    // The loop is in LCSSA form, so it reaches the exit only through phis.
    for (PHINode &PN : Exit->phis()) {
      SE.forgetLcssaPhiWithNewPredecessor(V, &PN);
      Value *V = PN.getIncomingValue(0);
      if (Value *Cloned = VMap.lookup(V))
        V = Cloned;
      PN.addIncoming(V, Slow->getExitingBlock());
    }
    formDedicatedExitBlocks(Slow, &DT, &LI, nullptr, true);
    formDedicatedExitBlocks(V, &DT, &LI, nullptr, true);
    for (Loop *SL : Slow->getLoopsInPreorder()) {
      addStringMetadataToLoop(SL, WidenedTag);
      addStringMetadataToLoop(SL, StringRef("llvm.loop.unroll.disable"));
    }
  }
  addStringMetadataToLoop(L, WidenedTag);
  if (V != L)
    addStringMetadataToLoop(V, WidenedTag);

  SCEVExpander Exp(SE, "widen");
  Exp.disableCanonicalMode();
  SmallVector<WeakTrackingVH, 4> Dead;
  for (auto [ZI, AR] : Widen) {
    Value *V = Exp.expandCodeFor(AR, ZI->getType(), ZI->getIterator());
    ZI->replaceAllUsesWith(V);
    Dead.push_back(ZI);
  }
  SE.forgetLoop(V);
  RecursivelyDeleteTriviallyDeadInstructions(Dead);
  // The ranges it cached may be of edges the versioning replaced.
  LVI.clear();
  return true;
}

PreservedAnalyses IA64WidenIndicesPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  // The versioned loops made coremark 3.1% faster on a Madison, but 7.2%
  // slower on an Itanium 9340.
  if (!EnableWidenIndices || !TM.getSubtargetImpl(F)->isMadison())
    return PreservedAnalyses::all();
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &LVI = AM.getResult<LazyValueAnalysis>(F);
  SmallVector<Loop *, 8> Innermost;
  for (Loop *L : LI.getLoopsInPreorder())
    if (L->isInnermost())
      Innermost.push_back(L);
  bool Changed = false;
  for (Loop *L : Innermost)
    Changed |= widenLoop(F, L, LI, DT, SE, AC, LVI);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<LoopAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
