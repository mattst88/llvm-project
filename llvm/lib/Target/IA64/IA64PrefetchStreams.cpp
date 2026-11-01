//===-- IA64PrefetchStreams.cpp - Prefetch loop streams into L2 -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LoopDataPrefetch fetches lines a loop skips over; it does nothing for a loop
// that walks memory an element at a time, and nothing for floating-point
// loads, which read L2. On Madison, whose 3 MB L3 holds most of what falls out
// of its 256 KB L2, such a loop waits for L2 fills. This pass gives its
// streams an lfetch.nt1, which fills L2 and leaves L1D alone, well ahead of
// the loads:
//
//  - of double or wider elements only, as a stride of bytes pays for an
//    lfetch on every one of a line's elements;
//  - two to a loop, which is all that tramp3d's stencils need; and
//  - unless all that the nest walks is known to fit in L2. An lfetch of a
//    line already there does nothing but compete with the FP loads: Polybench
//    jacobi-2d lost 20% to a single one per loop.
//
// Must run before HardwareLoops, which hides loop bounds from ScalarEvolution.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "ia64-prefetch-streams"

static cl::opt<unsigned>
    StreamDistance("ia64-stream-prefetch-distance", cl::Hidden, cl::init(800),
                   cl::desc("Bytes ahead of a stream's load to prefetch"));
static cl::opt<unsigned>
    MaxStreams("ia64-stream-prefetch-max", cl::Hidden, cl::init(2),
               cl::desc("Most streams of a loop to prefetch"));
static cl::opt<unsigned> L2Bytes(
    "ia64-stream-prefetch-l2-bytes", cl::Hidden, cl::init(256 * 1024),
    cl::desc("Most a nest may be known to walk and go without prefetches"));

static constexpr unsigned MinElementBytes = 8;
static constexpr int64_t SameLineBytes = 128;

namespace {

class IA64PrefetchStreams : public FunctionPass {
public:
  static char ID;
  IA64PrefetchStreams() : FunctionPass(ID) {}

  StringRef getPassName() const override { return "IA64 stream prefetch"; }
  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
  }

private:
  bool runOnLoop(Loop *L, ScalarEvolution &SE, DominatorTree &DT);
};

struct Stream {
  const SCEVAddRecExpr *Rec;
  int64_t Step;
  Instruction *InsertPt;
};

} // end anonymous namespace

char IA64PrefetchStreams::ID = 0;

INITIALIZE_PASS_BEGIN(IA64PrefetchStreams, DEBUG_TYPE,
                      "IA64 stream prefetch", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(IA64PrefetchStreams, DEBUG_TYPE,
                    "IA64 stream prefetch", false, false)

FunctionPass *llvm::createIA64PrefetchStreamsPass() {
  return new IA64PrefetchStreams();
}

/// The bytes \p Rec walks across the whole of its loop nest, or std::nullopt
/// if no bound is to be had.
static std::optional<uint64_t> getKnownSpan(const SCEVAddRecExpr *Rec,
                                            ScalarEvolution &SE) {
  const Loop *Outermost = Rec->getLoop()->getOutermostLoop();
  uint64_t Span = 0;
  const SCEV *S = Rec;
  while (auto *R = dyn_cast<SCEVAddRecExpr>(S)) {
    auto *Step = dyn_cast<SCEVConstant>(R->getStepRecurrence(SE));
    if (!R->isAffine() || !Step)
      return std::nullopt;
    const SCEV *Taken = SE.getSymbolicMaxBackedgeTakenCount(R->getLoop());
    APInt Trips = APInt::getAllOnes(64);
    if (!isa<SCEVCouldNotCompute>(Taken)) {
      Trips = SE.getUnsignedRangeMax(Taken);
      if (Trips.getActiveBits() > 32)
        Trips = SE.getSignedRangeMax(Taken);
    }
    if (Trips.isNegative() || Trips.getActiveBits() > 32) {
      // A triangular nest's inner count depends on the outer index, and unroll
      // remainders can hide even its range. Its rows are still rows: take the
      // step of the next loop out as the span of this one.
      auto *Outer = dyn_cast<SCEVAddRecExpr>(R->getStart());
      auto *Row = Outer
                      ? dyn_cast<SCEVConstant>(Outer->getStepRecurrence(SE))
                      : nullptr;
      if (!Row)
        return std::nullopt;
      Span += Row->getAPInt().abs().getLimitedValue(1 << 20);
    } else {
      Span += Step->getAPInt().abs().getLimitedValue(1 << 20) *
              Trips.getZExtValue();
    }
    S = R->getStart();
  }
  if (!SE.isLoopInvariant(S, Outermost))
    return std::nullopt;
  return Span;
}

/// Whether the streams of a nest, some of which may share an object, are known
/// to fit in L2.
static bool fitsInL2(ArrayRef<Stream> Streams, ScalarEvolution &SE) {
  SmallDenseMap<const SCEV *, uint64_t, 4> Objects;
  for (const Stream &S : Streams) {
    std::optional<uint64_t> Span = getKnownSpan(S.Rec, SE);
    if (!Span)
      return false;
    uint64_t &Object = Objects[SE.getPointerBase(S.Rec)];
    Object = std::max(Object, *Span);
  }
  uint64_t Known = 0;
  for (const auto &Object : Objects)
    Known += Object.second;
  return Known <= L2Bytes;
}

bool IA64PrefetchStreams::runOnLoop(Loop *L, ScalarEvolution &SE,
                                    DominatorTree &DT) {
  if (!L->getLoopPreheader() || !L->getLoopLatch())
    return false;

  SmallVector<Stream, 8> Streams;

  for (BasicBlock *BB : L->blocks())
    for (Instruction &I : *BB) {
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (!isa<IntrinsicInst>(CB))
          return false;
      auto *LI = dyn_cast<LoadInst>(&I);
      if (!LI || LI->getDataLayout().getTypeStoreSize(LI->getType()) <
                     MinElementBytes)
        continue;
      Value *Ptr = LI->getPointerOperand();
      if (L->isLoopInvariant(Ptr))
        continue;
      auto *Rec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(Ptr));
      if (!Rec || Rec->getLoop() != L || !Rec->isAffine())
        continue;
      auto *Step = dyn_cast<SCEVConstant>(Rec->getStepRecurrence(SE));
      if (!Step || Step->getValue()->isZero())
        continue;
      // Each pass of the next loop out either walks the same bytes again,
      // which the first left cached, or, for an FP row reached through its own
      // pointer, starts somewhere nothing says is far from the last row. Both
      // cost scimark 1.5% together; integer rows through pointers help zstd.
      if (Loop *Outer = L->getParentLoop()) {
        const SCEV *Start = Rec->getStart();
        if (SE.isLoopInvariant(Start, Outer) ||
            (LI->getType()->isFloatingPointTy() &&
             !SE.isLoopInvariant(SE.getPointerBase(Start), Outer)))
          continue;
      }

      bool Dup = false;
      for (Stream &S : Streams) {
        auto *Diff = dyn_cast<SCEVConstant>(SE.getMinusSCEV(Rec, S.Rec));
        if (!Diff || Diff->getAPInt().abs().uge(SameLineBytes))
          continue;
        Dup = true;
        if (S.InsertPt->getParent() != BB) {
          BasicBlock *Dom =
              DT.findNearestCommonDominator(S.InsertPt->getParent(), BB);
          if (Dom != S.InsertPt->getParent())
            S.InsertPt = Dom->getTerminator();
        }
        break;
      }
      if (!Dup)
        Streams.push_back({Rec, Step->getValue()->getSExtValue(), LI});
    }

  if (Streams.empty() || fitsInL2(Streams, SE))
    return false;

  bool Changed = false;
  unsigned Count = 0;
  for (Stream &S : Streams) {
    if (Count == MaxStreams)
      break;
    Instruction *At = S.InsertPt;
    if (!DT.dominates(At->getParent(), L->getLoopLatch()))
      continue;
    int64_t Ahead = S.Step < 0 ? -int64_t(StreamDistance) : StreamDistance;
    const SCEV *Next = SE.getAddExpr(
        S.Rec, SE.getConstant(S.Rec->getType(), Ahead, /*isSigned=*/true));
    SCEVExpander Expander(SE, "stream");
    if (!Expander.isSafeToExpand(Next))
      continue;
    Type *PtrTy = PointerType::get(At->getContext(), 0);
    Value *Addr = Expander.expandCodeFor(Next, PtrTy, At);
    IRBuilder<> B(At);
    Type *I32 = B.getInt32Ty();
    // Locality 2 selects .nt1, which leaves L1D alone.
    B.CreateIntrinsic(Intrinsic::prefetch, PtrTy,
                      {Addr, ConstantInt::get(I32, 0),
                       ConstantInt::get(I32, 2), ConstantInt::get(I32, 1)});
    ++Count;
    Changed = true;
  }
  return Changed;
}

bool IA64PrefetchStreams::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;
  auto &TM = getAnalysis<TargetPassConfig>().getTM<IA64TargetMachine>();
  if (!TM.getSubtargetImpl(F)->isMadison())
    return false;

  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  // By hand rather than required of the pass manager: that would run it here
  // for every tuning, and change what HardwareLoops makes of their loops.
  bool Changed = false;
  for (Loop *L : LI)
    Changed |= simplifyLoop(L, &DT, &LI, &SE, /*AC=*/nullptr, /*MSSAU=*/nullptr,
                            /*PreserveLCSSA=*/false);
  for (Loop *L : LI.getLoopsInPreorder())
    if (L->isInnermost())
      Changed |= runOnLoop(L, SE, DT);
  return Changed;
}
