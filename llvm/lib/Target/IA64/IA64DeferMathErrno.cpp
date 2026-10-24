//===-- IA64DeferMathErrno.cpp - Set a loop's sqrt errno after it ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A call to sqrt sets errno when its argument is negative, so it stays a call
// unless -fno-math-errno is given. Code generation expands it inline and calls
// the library only for a negative argument, but the call that remains in a
// loop keeps the loop from being pipelined, and as a write to memory, keeps
// loads of integers from being hoisted out of it.
//
// When nothing else in a single-block loop can read or write errno, the loop
// need not set errno until it exits. This pass replaces each sqrt in such a
// loop with the intrinsic, notes whether any argument was negative, and after
// the loop calls sqrt with a negative argument if one was, which sets errno
// as the last failing call in the loop would have.
//
// An access of the size of an integer may be to errno itself. When those
// accesses are at addresses that do not change in the loop, the loop is
// versioned on errno being elsewhere, as __errno_location returns it, and the
// copy that runs when it is not keeps the calls.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-defer-math-errno"

STATISTIC(NumDeferred, "Number of loops whose sqrt errno is set after them");
STATISTIC(NumVersioned, "Number of loops versioned on the address of errno");

/// Marks the copy of a versioned loop that keeps its calls.
static constexpr char VersionedMD[] = "llvm.loop.ia64.errno.versioned";

/// Whether \p CB is a call of sqrt or sqrtf that may set errno.
static bool isErrnoSqrt(const CallBase &CB, const TargetLibraryInfo &TLI) {
  LibFunc LF = TLI.getLibFunc(CB);
  return (LF == LibFunc_sqrt || LF == LibFunc_sqrtf) && TLI.has(LF) &&
         !CB.doesNotAccessMemory() && CB.arg_size() == 1;
}

namespace {
/// An access of loop-invariant address that may be to errno.
struct ErrnoCheck {
  Value *Ptr;
  uint64_t Size;
};
} // namespace

/// Finds the sqrt calls of \p L and the accesses that would need a check that
/// they are not to errno. Returns false if errno may be read or written in
/// some other way.
static bool analyzeLoop(const Loop &L, const TargetLibraryInfo &TLI,
                        AAResults &AA, SmallVectorImpl<CallInst *> &Calls,
                        SmallVectorImpl<ErrnoCheck> &Checks) {
  for (Instruction &I : *L.getHeader())
    if (auto *CI = dyn_cast<CallInst>(&I); CI && isErrnoSqrt(*CI, TLI))
      Calls.push_back(CI);
  if (Calls.empty())
    return false;
  for (Instruction &I : *L.getHeader()) {
    if (isa<CallInst>(I) && is_contained(Calls, &I))
      continue;
    // A pass that fails a sqrt must go on to the exit to set errno there.
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      return false;
    if (!I.mayReadOrWriteMemory())
      continue;
    auto *Load = dyn_cast<LoadInst>(&I);
    auto *Store = dyn_cast<StoreInst>(&I);
    if (!(Load && Load->isSimple()) && !(Store && Store->isSimple()))
      return false;
    MemoryLocation Loc = MemoryLocation::get(&I);
    if (isNoModRef(AA.getModRefInfo(Calls.front(), Loc)))
      continue;
    Value *Ptr = getLoadStorePointerOperand(&I);
    if (!L.isLoopInvariant(Ptr) || !Loc.Size.hasValue() ||
        Loc.Size.isScalable())
      return false;
    Checks.push_back({Ptr, Loc.Size.getValue().getFixedValue()});
  }
  return true;
}

/// Versions \p L on errno being at none of the addresses of \p Checks. The
/// copy that runs when it may be at one keeps its calls.
static void versionLoop(Loop &L, ArrayRef<ErrnoCheck> Checks,
                        const TargetLibraryInfo &TLI, LoopInfo &LI,
                        DominatorTree &DT) {
  BasicBlock *CheckBB = L.getLoopPreheader();
  BasicBlock *Exit = L.getExitBlock();
  Module &M = *CheckBB->getModule();
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  Type *IntPtrTy = DL.getIntPtrType(Ctx);

  // errno is where __errno_location says, which does not change in a thread.
  FunctionCallee ErrnoLoc = M.getOrInsertFunction(
      "__errno_location", FunctionType::get(PointerType::get(Ctx, 0), false));
  if (auto *ErrnoFn = dyn_cast<Function>(ErrnoLoc.getCallee())) {
    ErrnoFn->setDoesNotThrow();
    ErrnoFn->setDoesNotAccessMemory();
    ErrnoFn->setWillReturn();
  }
  IRBuilder<> B(CheckBB->getTerminator());
  Value *Errno = B.CreatePtrToInt(B.CreateCall(ErrnoLoc), IntPtrTy, "errno");
  Value *ErrnoEnd =
      B.CreateAdd(Errno, ConstantInt::get(IntPtrTy, TLI.getIntSize() / 8));
  Value *Apart = nullptr;
  for (const ErrnoCheck &C : Checks) {
    Value *Start = B.CreatePtrToInt(C.Ptr, IntPtrTy);
    Value *End = B.CreateAdd(Start, ConstantInt::get(IntPtrTy, C.Size));
    Value *Before = B.CreateICmpULE(End, Errno);
    Value *After = B.CreateICmpULE(ErrnoEnd, Start);
    Value *Ok = B.CreateOr(Before, After);
    Apart = Apart ? B.CreateAnd(Apart, Ok) : Ok;
  }
  Apart->setName("errno.apart");

  BasicBlock *PH = SplitBlock(CheckBB, CheckBB->getTerminator(), &DT, &LI,
                              nullptr, L.getHeader()->getName() + ".ph");
  ValueToValueMapTy VMap;
  SmallVector<BasicBlock *, 2> Blocks;
  Loop *Orig = cloneLoopWithPreheader(PH, CheckBB, &L, VMap, ".errno.orig", &LI,
                                      &DT, Blocks);
  remapInstructionsInBlocks(Blocks, VMap);
  addStringMetadataToLoop(Orig, VersionedMD);

  Instruction *Term = CheckBB->getTerminator();
  CondBrInst::Create(Apart, PH, Orig->getLoopPreheader(), Term->getIterator());
  Term->eraseFromParent();
  DT.changeImmediateDominator(Exit, CheckBB);

  // The exit's phis, which LCSSA form puts every value used after the loop
  // in, take the copy's value from it.
  for (PHINode &Phi : Exit->phis()) {
    Value *V = Phi.getIncomingValueForBlock(L.getHeader());
    if (Value *Copy = VMap.lookup(V))
      V = Copy;
    Phi.addIncoming(V, Orig->getHeader());
  }
  formDedicatedExitBlocks(Orig, &DT, &LI, nullptr, true);
  formDedicatedExitBlocks(&L, &DT, &LI, nullptr, true);
}

/// Replaces the sqrt calls of \p L with the intrinsic, and sets errno after
/// the loop if any of their arguments was negative.
static void deferErrno(Loop &L, ArrayRef<CallInst *> Calls, LoopInfo &LI,
                       DominatorTree &DT) {
  BasicBlock *Header = L.getHeader();
  BasicBlock *Exit = L.getExitBlock();
  LLVMContext &Ctx = Header->getContext();
  Type *I1 = Type::getInt1Ty(Ctx);

  PHINode *Failed = PHINode::Create(I1, 2, "sqrt.failed", Header->begin());
  Value *Next = Failed;
  IRBuilder<> B(Ctx);
  for (CallInst *CI : Calls) {
    B.SetInsertPoint(CI);
    Value *X = CI->getArgOperand(0);
    Value *Neg = B.CreateFCmpOLT(X, ConstantFP::getZero(X->getType()));
    Next = B.CreateOr(Next, Neg);
    Value *Sqrt = B.CreateUnaryIntrinsic(Intrinsic::sqrt, X, CI);
    Sqrt->takeName(CI);
    CI->replaceAllUsesWith(Sqrt);
  }
  Failed->addIncoming(ConstantInt::getFalse(Ctx), L.getLoopPreheader());
  Failed->addIncoming(Next, Header);
  Next->setName("sqrt.failed.next");

  // The last of the calls to fail would have set errno to EDOM, as a call
  // with any negative argument does.
  PHINode *Lcssa = PHINode::Create(I1, 1, "sqrt.failed.lcssa", Exit->begin());
  Lcssa->addIncoming(Next, Header);
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  Instruction *Then = SplitBlockAndInsertIfThen(
      Lcssa, Exit->getFirstInsertionPt(), /*Unreachable=*/false,
      MDBuilder(Ctx).createUnlikelyBranchWeights(), &DTU, &LI);
  CallInst *Orig = Calls.front();
  Type *Ty = Orig->getType();
  CallInst *SetErrno =
      CallInst::Create(Orig->getFunctionType(), Orig->getCalledOperand(),
                       {ConstantFP::get(Ty, -1.0)}, "", Then->getIterator());
  SetErrno->setAttributes(Orig->getAttributes());
  SetErrno->setCallingConv(Orig->getCallingConv());
  SetErrno->setDebugLoc(Orig->getDebugLoc());
  for (CallInst *CI : Calls)
    CI->eraseFromParent();
}

PreservedAnalyses IA64DeferMathErrnoPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  if (F.hasOptSize() || F.hasFnAttribute("no-builtins") ||
      F.hasFnAttribute(Attribute::StrictFP))
    return PreservedAnalyses::all();
  auto &LI = AM.getResult<LoopAnalysis>(F);
  if (LI.empty())
    return PreservedAnalyses::all();
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);

  SmallVector<Loop *> Loops;
  for (Loop *L : LI.getLoopsInPreorder())
    if (L->isInnermost() && L->getNumBlocks() == 1 &&
        !findStringMetadataForLoop(L, VersionedMD))
      Loops.push_back(L);

  bool Changed = false;
  for (Loop *L : Loops) {
    SmallVector<CallInst *> Calls;
    SmallVector<ErrnoCheck> Checks;
    if (!analyzeLoop(*L, TLI, AA, Calls, Checks))
      continue;
    Changed |= simplifyLoop(L, &DT, &LI, &SE, &AC, nullptr, false);
    if (!L->isLoopSimplifyForm() || !L->getExitBlock())
      continue;
    Changed |= formLCSSA(*L, DT, &LI, &SE);
    if (!Checks.empty()) {
      versionLoop(*L, Checks, TLI, LI, DT);
      ++NumVersioned;
    }
    deferErrno(*L, Calls, LI, DT);
    ++NumDeferred;
    Changed = true;
  }
  if (!Changed)
    return PreservedAnalyses::all();
  SE.forgetAllLoops();
  return PreservedAnalyses::none();
}
