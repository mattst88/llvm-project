//===-- IA64ShortenRecurrences.cpp - Add a loop-carried value last --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CPython's long multiplication runs
//
//   carry += *pz + *pa++ * f;
//   *pz++ = (digit)(carry & PyLong_MASK);
//   carry >>= PyLong_SHIFT;
//
// and Reassociate, which ranks a phi below every instruction of its loop,
// rewrites the sum as (carry + *pz) + *pa * f. Two adds and the shift then
// lie on the carry's path from one iteration to the next, and the software
// pipeliner cannot start an iteration more often than every three cycles,
// though only an add and the shift need wait for the previous carry.
//
// In a loop's header block this pass turns (P op X) op Y, where P is a phi
// of the header carried around the loop, into P op (X op Y) for an
// associative and commutative integer op. X op Y is then off the
// recurrence, and the loop-carried path is shorter by one op for each
// operand moved.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-shorten-recurrences"

STATISTIC(NumMoved, "Number of operands moved off a loop-carried path");

namespace {
class IA64ShortenRecurrences : public FunctionPass {
public:
  static char ID;
  IA64ShortenRecurrences() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "IA64 shorten loop-carried recurrences";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }
};
} // end anonymous namespace

char IA64ShortenRecurrences::ID = 0;
INITIALIZE_PASS_BEGIN(IA64ShortenRecurrences, DEBUG_TYPE,
                      "IA64 shorten loop-carried recurrences", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(IA64ShortenRecurrences, DEBUG_TYPE,
                    "IA64 shorten loop-carried recurrences", false, false)

FunctionPass *llvm::createIA64ShortenRecurrencesPass() {
  return new IA64ShortenRecurrences();
}

static bool isReassociable(const Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return I->getType()->isIntegerTy();
  default:
    return false;
  }
}

// The instructions of block BB that depend on P through operands in BB.
static void collectDependents(PHINode *P, BasicBlock *BB,
                              SmallPtrSetImpl<Instruction *> &Deps) {
  SmallVector<Instruction *, 16> Work{P};
  while (!Work.empty()) {
    Instruction *I = Work.pop_back_val();
    for (User *U : I->users()) {
      auto *UI = dyn_cast<Instruction>(U);
      if (UI && UI->getParent() == BB && !isa<PHINode>(UI) &&
          Deps.insert(UI).second)
        Work.push_back(UI);
    }
  }
}

// Rotate P past the ops above it, one at a time, for as long as the next op
// up is of the same kind and the only user.
static bool shortenFrom(PHINode *P, Loop &L) {
  BasicBlock *BB = L.getHeader();
  Value *Next = P->getIncomingValueForBlock(L.getLoopLatch());
  auto *NextI = dyn_cast<Instruction>(Next);
  if (!NextI || NextI->getParent() != BB)
    return false;

  SmallPtrSet<Instruction *, 16> Deps;
  collectDependents(P, BB, Deps);
  if (!Deps.count(NextI))
    return false;

  bool Changed = false;
  for (bool Again = true; Again;) {
    Again = false;
    for (User *U : P->users()) {
      auto *Inner = dyn_cast<BinaryOperator>(U);
      if (!Inner || Inner->getParent() != BB || !isReassociable(Inner) ||
          !Inner->hasOneUse())
        continue;
      unsigned PIdx = Inner->getOperand(0) == P ? 0 : 1;
      Value *X = Inner->getOperand(1 - PIdx);
      if (auto *XI = dyn_cast<Instruction>(X); X == P || (XI && Deps.count(XI)))
        continue;
      auto *Outer = dyn_cast<BinaryOperator>(Inner->user_back());
      if (!Outer || Outer->getParent() != BB ||
          Outer->getOpcode() != Inner->getOpcode())
        continue;
      unsigned InnerIdx = Outer->getOperand(0) == Inner ? 0 : 1;
      Value *Y = Outer->getOperand(1 - InnerIdx);
      // Y must be ready before the phi's value is; otherwise the new op
      // waits for it and the path gets no shorter.
      if (auto *YI = dyn_cast<Instruction>(Y); YI && Deps.count(YI))
        continue;

      IRBuilder<> B(Outer);
      Value *XY =
          B.CreateBinOp(Inner->getOpcode(), X, Y, Inner->getName() + ".offrec");
      Outer->setOperand(InnerIdx, P);
      Outer->setOperand(1 - InnerIdx, XY);
      // The new grouping of the sum can wrap where the old one did not.
      Outer->dropPoisonGeneratingFlags();
      Deps.erase(Inner);
      Inner->eraseFromParent();
      ++NumMoved;
      Changed = Again = true;
      break;
    }
  }
  return Changed;
}

bool IA64ShortenRecurrences::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;
  // The shorter recurrences made coremark 3.7% and python 2.1% slower on an
  // Itanium 9340.
  auto &TM = getAnalysis<TargetPassConfig>().getTM<IA64TargetMachine>();
  if (!TM.getSubtargetImpl(F)->isMadison())
    return false;
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  bool Changed = false;
  for (Loop *L : LI.getLoopsInPreorder()) {
    if (!L->isInnermost() || !L->getLoopLatch())
      continue;
    for (PHINode &P : L->getHeader()->phis())
      if (P.getType()->isIntegerTy())
        Changed |= shortenFrom(&P, *L);
  }
  return Changed;
}
