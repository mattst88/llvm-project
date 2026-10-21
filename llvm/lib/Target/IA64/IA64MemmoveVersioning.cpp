//===-- IA64MemmoveVersioning.cpp - Version memmove on overlap ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A small memmove is expanded to all of its loads followed by all of its
// stores, since a store may overwrite what a later load reads. A memmove of
// sixteen unaligned bytes then takes eight groups to load and eight more to
// store. A memcpy of the same bytes issues loads alongside stores, as the two
// do not overlap.
//
// This pass tests whether the operands of a small memmove overlap, and copies
// with a memcpy when they do not.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-memmove-versioning"

STATISTIC(NumVersioned, "Number of memmoves versioned on overlap");

static cl::opt<unsigned>
    MinAccesses("ia64-memmove-versioning-min-accesses", cl::Hidden, cl::init(8),
                cl::desc("The fewest loads a memmove is expanded to that is "
                         "versioned on overlap"));

static cl::opt<unsigned>
    MaxSize("ia64-memmove-versioning-max-size", cl::Hidden, cl::init(32),
            cl::desc("The largest memmove, in bytes, that is versioned on "
                     "overlap"));

/// The length of \p MM if it is worth versioning, and 0 otherwise.
static uint64_t getVersionedSize(const MemMoveInst &MM) {
  auto *Len = dyn_cast<ConstantInt>(MM.getLength());
  if (!Len || MM.isVolatile() || MM.getDestAddressSpace() != 0 ||
      MM.getSourceAddressSpace() != 0)
    return 0;
  uint64_t Size = Len->getZExtValue();
  Align A = std::min({MM.getDestAlign().valueOrOne(),
                      MM.getSourceAlign().valueOrOne(), Align(8)});
  if (Size > MaxSize || Size / A.value() < MinAccesses)
    return 0;
  return Size;
}

static void versionMemmove(MemMoveInst &MM, uint64_t Size) {
  IRBuilder<> B(&MM);
  // The operands overlap when the destination is less than Size bytes either
  // side of the source.
  Value *Diff =
      B.CreateSub(B.CreatePtrToInt(MM.getRawDest(), B.getInt64Ty()),
                  B.CreatePtrToInt(MM.getRawSource(), B.getInt64Ty()));
  Value *Overlap = B.CreateICmpULT(B.CreateAdd(Diff, B.getInt64(Size - 1)),
                                   B.getInt64(2 * Size - 1), "overlap");
  Instruction *Copy, *Move;
  // A memmove is written where the operands may overlap, which they seldom
  // do.
  SplitBlockAndInsertIfThenElse(
      Overlap, &MM, &Move, &Copy,
      MDBuilder(MM.getContext()).createUnlikelyBranchWeights());
  B.SetInsertPoint(Copy);
  CallInst *CI =
      B.CreateMemCpy(MM.getRawDest(), MM.getDestAlign(), MM.getRawSource(),
                     MM.getSourceAlign(), MM.getLength());
  CI->setAAMetadata(MM.getAAMetadata());
  MM.moveBefore(Move->getIterator());
  ++NumVersioned;
}

PreservedAnalyses IA64MemmoveVersioningPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  if (F.hasOptSize())
    return PreservedAnalyses::all();
  SmallVector<std::pair<MemMoveInst *, uint64_t>> Candidates;
  for (Instruction &I : instructions(F))
    if (auto *MM = dyn_cast<MemMoveInst>(&I))
      if (uint64_t Size = getVersionedSize(*MM))
        Candidates.push_back({MM, Size});
  for (auto [MM, Size] : Candidates)
    versionMemmove(*MM, Size);
  return Candidates.empty() ? PreservedAnalyses::all()
                            : PreservedAnalyses::none();
}
