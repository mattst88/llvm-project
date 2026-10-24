//===-- IA64SpeculateChases.cpp - Search a linked list ahead with ld.s ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CoreMark's core_list_find searches a list:
//
//   while (list && list->info->idx != idx)
//     list = list->next;
//
// Each node is a chain of loads, list->info then info->idx, and a compare,
// and the next node's loads cannot go above the branches on this one, since
// at the end of the list they would read through a null pointer. The loop
// takes about 7 cycles a node. A control-speculative load (ld.s) gives NaT
// instead of faulting, and NaT propagates through later ld.s and arithmetic,
// so the next node's loads can start with this one's if what a compare or
// the code after the loop reads is tested with tnat first.
//
// This pass finds an innermost loop whose only phi is the pointer it
// follows, whose blocks are a chain each ending in a compare and a branch
// out of the loop, and which does nothing but loads, address arithmetic and
// integer operations. Ahead of it, it puts a loop that does several of its
// iterations a pass, every load but those of the first block of the first
// iteration an ld.s at the top, and each branch preceded by a test that what
// it and the exit read is not NaT. If one is, the original loop takes over
// from the pointer that pass started with.
//
// Written by hand, core_list_find two nodes a pass took 5.2% off CoreMark
// on an Itanium 9340.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsIA64.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "ia64-speculate-chases"

STATISTIC(NumSpeculated, "Number of pointer-chasing loops speculated");
STATISTIC(NumPipelined, "Number of pointer-chasing loops pipelined");

static cl::opt<unsigned>
    ChaseCopies("ia64-chase-copies", cl::Hidden, cl::init(3),
                cl::desc("Iterations of a pointer-chasing search that a pass "
                         "of its speculative loop does"));

namespace {

// A loop following a pointer, and its blocks from the header to the latch.
struct Chase {
  Loop *L;
  PHINode *Ptr;
  SmallVector<BasicBlock *, 4> Blocks;
};

// A loop following a pointer that it loads at the top, which cannot be
// searched ahead, but whose next pointer can be loaded an iteration early.
struct Pipeline {
  Loop *L;
  PHINode *Ptr;
  LoadInst *Next;
};

} // namespace

static bool isSpeculatableLoad(const LoadInst &LI, const DataLayout &DL) {
  if (!LI.isSimple() || LI.getPointerAddressSpace() != 0)
    return false;
  Type *Ty = LI.getType();
  if (Ty->isPointerTy())
    return DL.getTypeSizeInBits(Ty) == 64;
  return Ty->isIntegerTy(8) || Ty->isIntegerTy(16) || Ty->isIntegerTy(32) ||
         Ty->isIntegerTy(64);
}

// Operations that cannot trap, on integers and pointers, which propagate
// NaT.
static bool isSpeculatableOp(const Instruction &I) {
  if (!I.getType()->isIntOrPtrTy() || any_of(I.operands(), [](const Use &U) {
        return !U->getType()->isIntOrPtrTy();
      }))
    return false;
  if (auto *BO = dyn_cast<BinaryOperator>(&I))
    return !BO->isIntDivRem();
  return isa<GetElementPtrInst, CastInst>(I);
}

static std::optional<Chase> matchChase(Loop *L, const DominatorTree &DT,
                                       const DataLayout &DL) {
  if (!L->isInnermost() || !L->getLoopPredecessor() || !L->getLoopLatch() ||
      !L->isLCSSAForm(DT))
    return std::nullopt;
  BasicBlock *Header = L->getHeader();
  PHINode *Ptr = nullptr;
  for (PHINode &Phi : Header->phis()) {
    if (Ptr)
      return std::nullopt;
    Ptr = &Phi;
  }
  if (!Ptr || !Ptr->getType()->isPointerTy())
    return std::nullopt;

  Chase C{L, Ptr, {}};
  unsigned Loads = 0, Ops = 0;
  BasicBlock *BB = Header;
  do {
    if (BB != Header && !BB->getSinglePredecessor())
      return std::nullopt;
    auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
    if (!Br ||
        L->contains(Br->getSuccessor(0)) == L->contains(Br->getSuccessor(1)))
      return std::nullopt;
    auto *Cmp = dyn_cast<ICmpInst>(Br->getCondition());
    if (!Cmp || Cmp->getParent() != BB || !Cmp->hasOneUse())
      return std::nullopt;
    for (Instruction &I : *BB) {
      if (&I == Ptr || &I == Cmp || &I == Br)
        continue;
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        if (!isSpeculatableLoad(*LI, DL))
          return std::nullopt;
        ++Loads;
      } else if (!isSpeculatableOp(I)) {
        return std::nullopt;
      }
      ++Ops;
    }
    C.Blocks.push_back(BB);
    BB = Br->getSuccessor(!L->contains(Br->getSuccessor(0)));
  } while (BB != Header && C.Blocks.size() < L->getNumBlocks());
  if (BB != Header || C.Blocks.size() != L->getNumBlocks())
    return std::nullopt;
  // The next pointer comes from a load. With one load there is no chain to
  // start early.
  auto *Next =
      dyn_cast<LoadInst>(Ptr->getIncomingValueForBlock(L->getLoopLatch()));
  if (!Next || !L->contains(Next) || Loads < 2 || Loads > 4 || Ops > 12)
    return std::nullopt;
  return C;
}

static std::optional<Pipeline> matchPipeline(Loop *L, const DominatorTree &DT,
                                             const DataLayout &DL) {
  if (!L->isInnermost() || !L->getLoopPredecessor() || !L->getLoopLatch() ||
      L->getNumBlocks() > 4 || !L->isLCSSAForm(DT))
    return std::nullopt;
  BasicBlock *Header = L->getHeader();
  BasicBlock *Latch = L->getLoopLatch();
  unsigned Ops = 0;
  for (BasicBlock *BB : L->blocks()) {
    if (!isa<CondBrInst, UncondBrInst>(BB->getTerminator()))
      return std::nullopt;
    for (Instruction &I : *BB) {
      if (isa<PHINode>(I) || I.isTerminator())
        continue;
      if (isa<CallBase>(I) || I.isAtomic() || I.isVolatile())
        return std::nullopt;
      ++Ops;
    }
  }
  if (Ops > 16)
    return std::nullopt;
  for (PHINode &Phi : Header->phis()) {
    if (!Phi.getType()->isPointerTy())
      continue;
    auto *Next = dyn_cast<LoadInst>(Phi.getIncomingValueForBlock(Latch));
    if (!Next || Next->getParent() != Header || !isSpeculatableLoad(*Next, DL))
      continue;
    // The address is the pointer, or a constant offset from it.
    Value *Addr = Next->getPointerOperand();
    if (auto *GEP = dyn_cast<GetElementPtrInst>(Addr))
      if (GEP->getPointerOperand() == &Phi && GEP->hasAllConstantIndices())
        Addr = &Phi;
    if (Addr != &Phi)
      continue;
    // Loaded at the end of the last iteration, the next pointer has to be
    // what the top of this one reads.
    if (any_of(make_range(Header->getFirstNonPHIIt(), Next->getIterator()),
               [](const Instruction &I) { return I.mayWriteToMemory(); }))
      continue;
    // Worth it if the loop waits for the pointer to decide whether to go on.
    if (none_of(Next->users(), [&](const User *U) {
          auto *Cmp = dyn_cast<ICmpInst>(U);
          return Cmp && any_of(Cmp->users(), [&](const User *V) {
                   auto *Br = dyn_cast<CondBrInst>(V);
                   return Br && L->isLoopExiting(Br->getParent());
                 });
        }))
      continue;
    return Pipeline{L, &Phi, Next};
  }
  return std::nullopt;
}

static Intrinsic::ID getSpeculativeLoad(unsigned Size) {
  switch (Size) {
  case 1:
    return Intrinsic::ia64_ld1_s;
  case 2:
    return Intrinsic::ia64_ld2_s;
  case 4:
    return Intrinsic::ia64_ld4_s;
  default:
    return Intrinsic::ia64_ld8_s;
  }
}

// The bytes of a load that are used: on a little-endian target, a load whose
// value only reaches a mask of low bytes, through operations whose low bits
// depend only on their operands' low bits, can read just those.
static unsigned getUsedSize(const LoadInst &LI, const DataLayout &DL) {
  unsigned Size = DL.getTypeStoreSize(LI.getType());
  if (DL.isBigEndian() || !LI.getType()->isIntegerTy())
    return Size;
  const Value *V = &LI;
  while (V->hasOneUse()) {
    auto *I = cast<Instruction>(V->user_back());
    const APInt *Mask;
    if (match(I, m_And(m_Value(), m_APInt(Mask))) && Mask->isMask()) {
      unsigned Used = PowerOf2Ceil(divideCeil(Mask->countr_one(), 8));
      return std::min(Size, std::max(Used, 1u));
    }
    if (!match(I, m_CombineOr(m_c_Xor(m_Specific(V), m_Value()),
                              m_c_Or(m_Specific(V), m_Value()))))
      break;
    V = I;
  }
  return Size;
}

static void speculate(const Chase &C, unsigned Copies, const DataLayout &DL) {
  Loop *L = C.L;
  BasicBlock *Header = C.Blocks.front();
  BasicBlock *Latch = C.Blocks.back();
  BasicBlock *Preheader = L->getLoopPreheader();
  Function &F = *Header->getParent();
  LLVMContext &Ctx = F.getContext();
  MDNode *Unlikely = MDBuilder(Ctx).createUnlikelyBranchWeights();

  // The speculative loop's header, which holds every load and operation of
  // a pass, and the block it leaves for the original loop through.
  auto *SpecHeader =
      BasicBlock::Create(Ctx, Header->getName() + ".spec", &F, Header);
  auto *NaT = BasicBlock::Create(Ctx, Header->getName() + ".nat", &F, Header);
  IRBuilder<> B(SpecHeader);
  PHINode *SpecPtr =
      B.CreatePHI(C.Ptr->getType(), 2, C.Ptr->getName() + ".spec");
  SpecPtr->addIncoming(C.Ptr->getIncomingValueForBlock(Preheader), Preheader);
  Preheader->getTerminator()->replaceSuccessorWith(Header, SpecHeader);
  int Idx = C.Ptr->getBasicBlockIndex(Preheader);
  C.Ptr->setIncomingBlock(Idx, NaT);
  C.Ptr->setIncomingValue(Idx, SpecPtr);
  UncondBrInst::Create(Header, NaT);

  // Each copy's values, and the speculative loads each value depends on,
  // only through operations other than loads: an ld.s from a NaT address
  // gives NaT.
  SmallVector<DenseMap<Value *, Value *>, 4> Maps(Copies);
  DenseMap<Value *, SmallSetVector<Value *, 2>> Leaves;
  auto Map = [&](unsigned Copy, Value *V) {
    auto It = Maps[Copy].find(V);
    return It == Maps[Copy].end() ? V : It->second;
  };
  auto Clone = [&](unsigned Copy, Instruction &I) {
    Instruction *New = I.clone();
    SmallSetVector<Value *, 2> &L = Leaves[New];
    for (Use &U : New->operands()) {
      U.set(Map(Copy, U));
      auto It = Leaves.find(U);
      if (It != Leaves.end())
        L.insert_range(It->second);
    }
    Maps[Copy][&I] = New;
    return New;
  };

  // What only a block's compare reads goes with the compare rather than at
  // the top, so that instruction selection, which sees one block at a time,
  // can fold it into the compare.
  SmallPtrSet<Instruction *, 8> Sunk;
  for (BasicBlock *BB : C.Blocks) {
    Value *Cmp = cast<CondBrInst>(BB->getTerminator())->getCondition();
    for (Instruction &I : reverse(*BB))
      if (!isa<PHINode, LoadInst, ICmpInst>(I) && !I.isTerminator() &&
          !I.user_empty() && all_of(I.users(), [&](const User *U) {
            return U == Cmp || Sunk.contains(cast<Instruction>(U));
          }))
        Sunk.insert(&I);
  }
  Value *Next = C.Ptr->getIncomingValueForBlock(Latch);
  Value *Cur = SpecPtr;
  for (unsigned Copy = 0; Copy != Copies; ++Copy) {
    Maps[Copy][C.Ptr] = Cur;
    for (auto [Index, BB] : enumerate(C.Blocks))
      for (Instruction &I : *BB) {
        if (isa<PHINode, ICmpInst, CondBrInst>(I) || Sunk.contains(&I))
          continue;
        auto *LI = dyn_cast<LoadInst>(&I);
        unsigned Used = LI ? getUsedSize(*LI, DL) : 0;
        if (LI && !Copy && !Index &&
            Used < DL.getTypeStoreSize(LI->getType())) {
          Type *Ty = LI->getType();
          LoadInst *Load = B.CreateAlignedLoad(
              B.getIntNTy(Used * 8), Map(Copy, LI->getPointerOperand()),
              LI->getAlign(), LI->getName() + ".low");
          Load->copyMetadata(*LI, {LLVMContext::MD_tbaa});
          Maps[Copy][LI] = B.CreateZExt(Load, Ty, LI->getName());
          continue;
        }
        if (LI && (Copy || Index)) {
          Type *Ty = LI->getType();
          Value *Load = B.CreateIntrinsic(getSpeculativeLoad(Used), {},
                                          {Map(Copy, LI->getPointerOperand())},
                                          nullptr, LI->getName() + ".s");
          Value *V = Ty->isPointerTy()
                         ? B.CreateIntToPtr(Load, Ty, LI->getName())
                         : B.CreateZExtOrTrunc(Load, Ty, LI->getName());
          Leaves[V].insert(Load);
          Maps[Copy][LI] = V;
          continue;
        }
        B.Insert(Clone(Copy, I), I.getName());
      }
    Cur = Map(Copy, Next);
  }

  // The branches, each after a test of what it and its exit read. A test
  // covers the later ones too.
  SmallPtrSet<Value *, 8> Tested;
  BasicBlock *At = SpecHeader;
  for (unsigned Copy = 0; Copy != Copies; ++Copy)
    for (auto [Index, BB] : enumerate(C.Blocks)) {
      auto *Br = cast<CondBrInst>(BB->getTerminator());
      auto *Cmp = cast<ICmpInst>(Br->getCondition());
      bool ExitsOnTrue = !L->contains(Br->getSuccessor(0));
      BasicBlock *Exit = Br->getSuccessor(!ExitsOnTrue);
      SmallVector<std::pair<Instruction *, Instruction *>, 4> Sinking;
      for (Instruction &I : *BB)
        if (Sunk.contains(&I))
          Sinking.emplace_back(Clone(Copy, I), &I);
      SmallSetVector<Value *, 4> Need;
      auto Read = [&](Value *V) {
        auto It = Leaves.find(Map(Copy, V));
        if (It != Leaves.end())
          for (Value *Leaf : It->second)
            if (!Tested.contains(Leaf))
              Need.insert(Leaf);
      };
      for (Value *Op : Cmp->operands())
        Read(Op);
      for (PHINode &Phi : Exit->phis())
        Read(Phi.getIncomingValueForBlock(BB));
      // The next pass's first loads are not speculative.
      bool Last = Copy == Copies - 1 && BB == Latch;
      if (Last)
        Read(Next);
      B.SetInsertPoint(At);
      B.SetCurrentDebugLocation(Br->getDebugLoc());
      if (!Need.empty()) {
        Value *Any = Need[0];
        for (Value *V : drop_begin(Need))
          Any = B.CreateOr(Any, V);
        Value *IsNaT = B.CreateICmpNE(
            B.CreateIntrinsic(Intrinsic::ia64_tnat, {}, {Any}), B.getInt64(0));
        auto *Test =
            BasicBlock::Create(Ctx, BB->getName() + ".spec", &F, Header);
        B.CreateCondBr(IsNaT, NaT, Test, Unlikely);
        Tested.insert_range(Need);
        At = Test;
        B.SetInsertPoint(At);
      }
      for (auto [New, I] : Sinking)
        B.Insert(New, I->getName());
      Instruction *NewCmp = Cmp->clone();
      for (Use &U : NewCmp->operands())
        U.set(Map(Copy, U));
      B.Insert(NewCmp, Cmp->getName());
      BasicBlock *Cont =
          Last ? SpecHeader
               : BasicBlock::Create(Ctx, BB->getName() + ".spec", &F, Header);
      B.CreateCondBr(NewCmp, ExitsOnTrue ? Exit : Cont,
                     ExitsOnTrue ? Cont : Exit,
                     Br->getMetadata(LLVMContext::MD_prof));
      for (PHINode &Phi : Exit->phis())
        Phi.addIncoming(Map(Copy, Phi.getIncomingValueForBlock(BB)), At);
      if (Last)
        SpecPtr->addIncoming(Cur, At);
      At = Cont;
    }
}

// Load the next pointer at the end of each iteration with an ld.s, after
// the iteration's stores, and at the top of the next one test it for NaT and
// load it again if it is. Then unroll the loop, so that the three pointers
// live at once, the one an iteration works on, the next one and the one
// loaded early, take turns in registers instead of being copied.
static void pipeline(const Pipeline &P, unsigned Copies, LoopInfo &LI,
                     DominatorTree &DT, ScalarEvolution &SE,
                     AssumptionCache &AC, const TargetTransformInfo &TTI) {
  Loop *L = P.L;
  SE.forgetLoop(L);
  simplifyLoop(L, &DT, &LI, &SE, &AC, nullptr, true);
  BasicBlock *Header = L->getHeader();
  BasicBlock *Preheader = L->getLoopPreheader();
  LLVMContext &Ctx = Header->getContext();
  Value *Addr = P.Next->getPointerOperand();
  auto LoadNext = [&](Value *Ptr, Instruction *Before) {
    IRBuilder<> B(Before);
    Value *A = Ptr;
    if (Addr != P.Ptr) {
      auto *GEP = cast<GetElementPtrInst>(Addr)->clone();
      GEP->setOperand(0, Ptr);
      A = B.Insert(GEP);
    }
    return B.CreateIntrinsic(Intrinsic::ia64_ld8_s, {}, {A}, nullptr,
                             P.Next->getName() + ".s");
  };

  IRBuilder<> B(&Header->front());
  PHINode *S = B.CreatePHI(B.getInt64Ty(), 2, P.Next->getName() + ".s");
  S->addIncoming(LoadNext(P.Ptr->getIncomingValueForBlock(Preheader),
                          Preheader->getTerminator()),
                 Preheader);
  BasicBlock *Cont = SplitBlock(Header, P.Next, &DT, &LI);
  auto *Reload = BasicBlock::Create(Ctx, Header->getName() + ".nat",
                                    Header->getParent(), Cont);
  L->addBasicBlockToLoop(Reload, LI);
  DT.addNewBlock(Reload, Header);
  Header->getTerminator()->eraseFromParent();
  B.SetInsertPoint(Header);
  B.SetCurrentDebugLocation(P.Next->getDebugLoc());
  Value *Spec = B.CreateIntToPtr(S, P.Next->getType(), P.Next->getName());
  Value *IsNaT = B.CreateICmpNE(
      B.CreateIntrinsic(Intrinsic::ia64_tnat, {}, {static_cast<Value *>(S)}),
      B.getInt64(0));
  B.CreateCondBr(IsNaT, Reload, Cont,
                 MDBuilder(Ctx).createUnlikelyBranchWeights());
  B.SetInsertPoint(Reload);
  Instruction *Plain = P.Next->clone();
  B.Insert(Plain, P.Next->getName());
  B.CreateBr(Cont);
  B.SetInsertPoint(Cont, Cont->begin());
  PHINode *V = B.CreatePHI(P.Next->getType(), 2);
  V->addIncoming(Spec, Header);
  V->addIncoming(Plain, Reload);
  P.Next->replaceAllUsesWith(V);
  V->takeName(P.Next);
  P.Next->eraseFromParent();
  BasicBlock *Latch = L->getLoopLatch();
  S->addIncoming(LoadNext(V, Latch->getTerminator()), Latch);

  UnrollLoopOptions ULO;
  ULO.Count = Copies;
  ULO.Force = true;
  ULO.Runtime = false;
  ULO.AllowExpensiveTripCount = false;
  ULO.UnrollRemainder = false;
  ULO.ForgetAllSCEV = false;
  ULO.SCEVExpansionBudget = 0;
  UnrollLoop(L, ULO, &LI, &SE, &DT, &AC, &TTI, nullptr,
             /*PreserveLCSSA=*/false);
}

PreservedAnalyses IA64SpeculateChasesPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  if (ChaseCopies < 2)
    return PreservedAnalyses::all();
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  const DataLayout &DL = F.getDataLayout();
  SmallVector<Chase, 2> Chases;
  SmallVector<Pipeline, 2> Pipelines;
  for (Loop *L : LI.getLoopsInPreorder())
    if (std::optional<Chase> C = matchChase(L, DT, DL))
      Chases.push_back(*C);
    else if (std::optional<Pipeline> P = matchPipeline(L, DT, DL))
      Pipelines.push_back(*P);
  if (Chases.empty() && Pipelines.empty())
    return PreservedAnalyses::all();
  // Pipelined loops are unrolled, which keeps the loop info and dominator
  // tree up to date for the rest.
  if (!Pipelines.empty()) {
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    auto &AC = AM.getResult<AssumptionAnalysis>(F);
    auto &TTI = AM.getResult<TargetIRAnalysis>(F);
    for (const Pipeline &P : Pipelines) {
      pipeline(P, ChaseCopies, LI, DT, SE, AC, TTI);
      ++NumPipelined;
    }
    // Splitting off a pipelined loop's dedicated exits can move a chase's
    // exit phi into a block past a new exit block of its own; match again.
    SmallVector<Chase, 2> Rematched;
    for (const Chase &C : Chases) {
      formLCSSA(*C.L, DT, &LI, &SE);
      if (std::optional<Chase> R = matchChase(C.L, DT, DL))
        Rematched.push_back(*R);
    }
    Chases = std::move(Rematched);
  }
  // A loop entered from a block that branches elsewhere too gets a
  // preheader to branch to the speculative loop from, while the dominator
  // tree is still up to date.
  for (const Chase &C : Chases)
    if (!C.L->getLoopPreheader())
      InsertPreheaderForLoop(C.L, &DT, &LI, nullptr, false);
  for (const Chase &C : Chases) {
    speculate(C, ChaseCopies, DL);
    ++NumSpeculated;
  }
  return PreservedAnalyses::none();
}
