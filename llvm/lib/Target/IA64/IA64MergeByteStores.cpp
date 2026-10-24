//===-- IA64MergeByteStores.cpp - Store bytes a doubleword at a time ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Polybench's print_element writes a double as 16 characters, a store of a
// byte each, to a buffer from malloc:
//
//   *(out+pos)   = (block.bytes[0]&0xF0>>4)+'0';
//   *(out+pos+1) = (block.bytes[0]&0x0F)   +'0';
//   ...
//
// On the Itanium 9340 a byte store to a doubleword that the store ahead of
// it also wrote leaves the store queue 6 cycles after that one, so the 16
// stores take about 90 cycles where two st8 take a few. Nothing combines
// them: their values are computed, which the DAG's store merging does not
// take, there are no vector types for the SLP vectorizer to use, and LLVM
// does not assume that what malloc returns is aligned.
//
// This pass finds, in a block, byte stores to every byte of one or more
// doublewords at constant offsets from one pointer, with nothing between
// the first and the last of them that reads or writes memory, and stores
// each doubleword with one st8 where the last of them was. Where the first
// doubleword is not known to be aligned, it tests its address and keeps the
// byte stores for when it is not.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <map>

using namespace llvm;

#define DEBUG_TYPE "ia64-merge-byte-stores"

STATISTIC(NumMerged, "Number of doublewords stored a byte at a time merged");
STATISTIC(NumVersioned, "Number of runs merged behind an alignment test");

namespace {

struct ByteStore {
  StoreInst *SI;
  int64_t Off;
};

// The byte stores from Begin on to one pointer with nothing between them
// that touches memory, the last store to each offset winning.
static Value *collectRun(BasicBlock::iterator Begin, BasicBlock::iterator End,
                         const DataLayout &DL,
                         SmallVectorImpl<ByteStore> &Run) {
  Value *Base = nullptr;
  for (Instruction &I : make_range(Begin, End)) {
    if (!I.mayReadOrWriteMemory())
      continue;
    auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI || !SI->isSimple() ||
        !SI->getValueOperand()->getType()->isIntegerTy(8))
      break;
    APInt Off(DL.getIndexTypeSizeInBits(SI->getPointerOperandType()), 0);
    Value *B = SI->getPointerOperand()->stripAndAccumulateConstantOffsets(
        DL, Off, /*AllowNonInbounds=*/true);
    if (Base && B != Base)
      break;
    Base = B;
    Run.push_back({SI, Off.getSExtValue()});
  }
  return Base;
}

static bool mergeRuns(BasicBlock &Entry) {
  const DataLayout &DL = Entry.getDataLayout();
  bool Changed = false;
  BasicBlock *BB = &Entry;
  for (BasicBlock::iterator It = BB->begin(); It != BB->end();) {
    if (!isa<StoreInst>(&*It)) {
      ++It;
      continue;
    }
    SmallVector<ByteStore, 16> Run;
    Value *Base = collectRun(It, BB->end(), DL, Run);
    if (Run.size() < 8) {
      ++It;
      continue;
    }

    // The value each offset ends up with, and the first run of whole
    // doublewords of them.
    std::map<int64_t, Value *> Bytes;
    for (ByteStore &S : Run)
      Bytes[S.Off] = S.SI->getValueOperand();
    int64_t Start = 0, Len = 0;
    for (auto [Off, V] : Bytes) {
      if (Len && Off == Start + Len) {
        ++Len;
        continue;
      }
      if (Len >= 8)
        break;
      Start = Off;
      Len = 1;
    }
    // Where the pointer is aligned, the doublewords that are.
    bool Aligned = getKnownAlignment(Base, DL) >= Align(8);
    if (Aligned) {
      int64_t Skip = alignTo(Start, 8) - Start;
      Start += Skip;
      Len = std::max<int64_t>(Len - Skip, 0);
    }
    Len &= ~int64_t(7);
    if (!Len) {
      It = std::next(Run.back().SI->getIterator());
      continue;
    }

    StoreInst *Last = Run.back().SI;
    IRBuilder<> B(Last);
    Value *Addr = Start ? B.CreatePtrAdd(Base, B.getInt64(Start)) : Base;

    Instruction *WideAt = Last;
    if (!Aligned) {
      Value *Low = B.CreateAnd(B.CreatePtrToInt(Addr, B.getInt64Ty()), 7);
      Value *Cond = B.CreateICmpEQ(Low, B.getInt64(0));
      Instruction *Then, *Else;
      MDNode *Weights = MDBuilder(BB->getContext()).createLikelyBranchWeights();
      SplitBlockAndInsertIfThenElse(Cond, Last->getIterator(), &Then, &Else,
                                    Weights, /*DTU=*/nullptr);
      // A byte at a time from the address of the first, for when it is not
      // aligned.
      B.SetInsertPoint(Else);
      for (int64_t K = 0; K < Len; ++K)
        B.CreateStore(Bytes[Start + K],
                      K ? B.CreatePtrAdd(Addr, B.getInt64(K)) : Addr);
      WideAt = Then;
      BB = Then->getParent()->getSingleSuccessor();
      ++NumVersioned;
    }

    B.SetInsertPoint(WideAt);
    StoreInst *Wide = nullptr;
    for (int64_t W = 0; W < Len; W += 8) {
      Value *Word = nullptr;
      for (int64_t K = 0; K < 8; ++K) {
        Value *V = B.CreateZExt(Bytes[Start + W + K], B.getInt64Ty());
        unsigned Shift = DL.isLittleEndian() ? 8 * K : 56 - 8 * K;
        if (Shift)
          V = B.CreateShl(V, Shift);
        Word = Word ? B.CreateOr(Word, V, "", /*IsDisjoint=*/true) : V;
      }
      Wide = B.CreateAlignedStore(
          Word, W ? B.CreatePtrAdd(Addr, B.getInt64(W)) : Addr, Align(8));
      ++NumMerged;
    }
    for (ByteStore &S : Run)
      if (S.Off >= Start && S.Off < Start + Len)
        S.SI->eraseFromParent();
    Changed = true;
    // Versioning moved what followed the stores to a block of its own.
    It = Aligned ? std::next(Wide->getIterator()) : BB->begin();
  }
  return Changed;
}

} // namespace

PreservedAnalyses IA64MergeByteStoresPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  bool Changed = false;
  SmallVector<BasicBlock *, 16> Blocks(llvm::make_pointer_range(F));
  for (BasicBlock *BB : Blocks)
    Changed |= mergeRuns(*BB);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
