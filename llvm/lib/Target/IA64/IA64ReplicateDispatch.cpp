//===-- IA64ReplicateDispatch.cpp - Give each handler its own dispatch ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An interpreter written as a switch in a loop dispatches every instruction
// through one branch, or below the jump-table threshold through one compare
// tree, and the branches at its leaves see every opcode the program runs.
// A copy at the end of each handler instead learns which opcode tends to
// follow that one.
//
// The machine tail duplicator would give each handler its own copy of a block
// ending in the branch, but the dispatch is rarely one block: Lua's fetches the
// instruction and tests for a debug hook first, and the call the hook makes
// sits between the two.  So do it here.  The region is the switch's block,
// with the block that branches to it and to a side block that rejoins it
// (the hook), and it is cloned into each predecessor that branches to it
// unconditionally, through a block that only gathers PHIs if there is one.
// The side block stays shared and rejoins the original switch.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-replicate-dispatch"
#define IA64_REPLICATE_DISPATCH_NAME "IA64 replicate switch dispatch"

static cl::opt<bool>
    EnableReplicateDispatch("ia64-enable-replicate-dispatch", cl::Hidden,
                            cl::init(true),
                            cl::desc("Give each handler of a switch-dispatched "
                                     "interpreter loop its own dispatch"));

// The fewest distinct case destinations, and the fewest handlers branching
// back, for which a switch counts as an interpreter's dispatch.
static cl::opt<unsigned> MinDispatchTargets("ia64-replicate-dispatch-min",
                                            cl::Hidden, cl::init(16));

// The most instructions in the cloned region.
static cl::opt<unsigned> MaxDispatchSize("ia64-replicate-dispatch-size",
                                         cl::Hidden, cl::init(24));

namespace {
class IA64ReplicateDispatch : public FunctionPass {
public:
  static char ID;
  IA64ReplicateDispatch() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return IA64_REPLICATE_DISPATCH_NAME;
  }
};

// The blocks of one dispatch: Head branches to Switch, or to Switch and to
// Side, from which the code rejoins Switch.  Head is Switch when there is no
// test.  Side may begin a region of its own: Lua's hook call is one block,
// but inlined across files with LTO it is many, and may throw.
struct Dispatch {
  BasicBlock *Head;
  BasicBlock *Side = nullptr;
  BasicBlock *Switch;
  // A block holding only PHIs and a branch to Head, through which the
  // handlers reach it.
  BasicBlock *Gather = nullptr;
};
} // end anonymous namespace

char IA64ReplicateDispatch::ID = 0;

INITIALIZE_PASS(IA64ReplicateDispatch, DEBUG_TYPE,
                IA64_REPLICATE_DISPATCH_NAME, false, false)

FunctionPass *llvm::createIA64ReplicateDispatch() {
  return new IA64ReplicateDispatch();
}

static bool isClonable(const BasicBlock &BB, unsigned &Size) {
  for (const Instruction &I : BB) {
    if (isa<PHINode>(I) || isa<DbgInfoIntrinsic>(I))
      continue;
    if (isa<CallBase>(I) || isa<AllocaInst>(I) || I.getType()->isTokenTy())
      return false;
    ++Size;
  }
  return true;
}

// A block that only merges values for Head, with no use of them elsewhere.
static bool isGather(const BasicBlock &BB, const BasicBlock *Head) {
  auto *Br = dyn_cast<UncondBrInst>(BB.getTerminator());
  if (!Br || Br->getSuccessor(0) != Head ||
      BB.getFirstNonPHIIt() != Br->getIterator())
    return false;
  for (const PHINode &PN : BB.phis())
    for (const User *U : PN.users())
      if (!isa<PHINode>(U) || cast<Instruction>(U)->getParent() != Head)
        return false;
  return true;
}

static bool findDispatch(SwitchInst *SI, Dispatch &D, const DominatorTree &DT) {
  D.Switch = SI->getParent();
  D.Head = D.Switch;
  SmallVector<BasicBlock *, 2> Preds(predecessors(D.Switch));
  if (Preds.size() == 2) {
    for (unsigned I = 0; I != 2; ++I) {
      // The side code is entered only from Head, and every path from it to
      // Switch arrives through Exit, which it dominates.
      BasicBlock *Exit = Preds[I], *Head = Preds[1 - I];
      auto *Br = dyn_cast<CondBrInst>(Head->getTerminator());
      if (!Br)
        continue;
      BasicBlock *Side = Br->getSuccessor(0) == D.Switch   ? Br->getSuccessor(1)
                         : Br->getSuccessor(1) == D.Switch ? Br->getSuccessor(0)
                                                           : nullptr;
      if (Side && Side != D.Switch && Side->getSinglePredecessor() == Head &&
          DT.dominates(Side, Exit)) {
        D.Head = Head;
        D.Side = Side;
        break;
      }
    }
  }
  if (D.Head == D.Switch && pred_size(D.Switch) == 1)
    return false;

  unsigned Size = 0;
  if (!isClonable(*D.Switch, Size) ||
      (D.Head != D.Switch && !isClonable(*D.Head, Size)) ||
      Size > MaxDispatchSize)
    return false;
  for (BasicBlock *Pred : predecessors(D.Head))
    if (isGather(*Pred, D.Head)) {
      D.Gather = Pred;
      break;
    }
  return true;
}

// The predecessors to give a copy: those reaching Head, or Gather, with an
// unconditional branch, other than the dispatch's own blocks.
static void findEntries(const Dispatch &D,
                        SmallVectorImpl<BasicBlock *> &Entries) {
  auto Add = [&](BasicBlock *To) {
    SmallPtrSet<BasicBlock *, 32> Seen;
    for (BasicBlock *Pred : predecessors(To)) {
      if (Pred == D.Head || Pred == D.Switch || Pred == D.Side ||
          Pred == D.Gather || !Seen.insert(Pred).second)
        continue;
      if (isa<UncondBrInst>(Pred->getTerminator()))
        Entries.push_back(Pred);
    }
  };
  if (D.Gather)
    Add(D.Gather);
  Add(D.Head);
}

static void replicate(const Dispatch &D, ArrayRef<BasicBlock *> Entries) {
  Function &F = *D.Head->getParent();
  SmallVector<BasicBlock *, 2> Region = {D.Head};
  if (D.Switch != D.Head)
    Region.push_back(D.Switch);

  // Values defined in the region, in order, with their copies.
  SmallVector<Instruction *, 32> Defs;
  for (BasicBlock *BB : Region)
    for (Instruction &I : *BB)
      if (!I.getType()->isVoidTy())
        Defs.push_back(&I);
  SmallVector<SmallVector<std::pair<BasicBlock *, Value *>, 0>, 32> Copies(
      Defs.size());

  for (BasicBlock *Entry : Entries) {
    BasicBlock *From = Entry->getSingleSuccessor();
    ValueToValueMapTy VMap;
    BasicBlock *NewHead = nullptr, *NewSwitch = nullptr;
    for (BasicBlock *BB : Region) {
      BasicBlock *New = BasicBlock::Create(
          F.getContext(), BB->getName() + ".rep", &F, Entry->getNextNode());
      VMap[BB] = New;
      if (BB == D.Head)
        NewHead = New;
      if (BB == D.Switch)
        NewSwitch = New;
      for (Instruction &I : *BB) {
        auto *PN = dyn_cast<PHINode>(&I);
        if (PN && BB != D.Head) {
          // Only the copy of Head reaches the copy of the switch.
          Value *V = PN->getIncomingValueForBlock(D.Head);
          Value *Mapped = VMap.lookup(V);
          VMap[PN] = Mapped ? Mapped : V;
          continue;
        }
        if (PN) {
          // Head's copy keeps a PHI with the one incoming value, from this
          // entry, through Gather's PHI if there is one.  The value may be one
          // the region defines, as it is at the entry, which the rewrite
          // below then finds for it.
          Value *V =
              PN->getIncomingValueForBlock(From == D.Gather ? D.Gather : Entry);
          if (auto *GPN = dyn_cast<PHINode>(V);
              GPN && From == D.Gather && GPN->getParent() == D.Gather)
            V = GPN->getIncomingValueForBlock(Entry);
          PHINode *NewPN =
              PHINode::Create(PN->getType(), 1, PN->getName(), New->end());
          NewPN->addIncoming(V, Entry);
          VMap[PN] = NewPN;
          continue;
        }
        Instruction *NewI = I.clone();
        NewI->setName(I.getName());
        NewI->insertInto(New, New->end());
        VMap[&I] = NewI;
      }
    }
    // Head's PHIs name what arrives from the entry, which is the original.
    for (BasicBlock *BB : Region)
      for (Instruction &I : *cast<BasicBlock>(VMap[BB]))
        if (!isa<PHINode>(I))
          RemapInstruction(&I, VMap,
                           RF_IgnoreMissingLocals | RF_NoModuleLevelChanges);

    // The copies' successors outside the region see them as new predecessors.
    auto AddIncoming = [&](BasicBlock *Old, BasicBlock *New) {
      SmallPtrSet<BasicBlock *, 32> Done;
      for (BasicBlock *Succ : successors(New)) {
        if (!Done.insert(Succ).second || Succ == NewSwitch)
          continue;
        for (PHINode &PN : Succ->phis())
          for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I)
            if (PN.getIncomingBlock(I) == Old) {
              Value *V = PN.getIncomingValue(I);
              if (auto It = VMap.find(V); It != VMap.end())
                V = It->second;
              PN.addIncoming(V, New);
            }
      }
    };
    AddIncoming(D.Head, NewHead);
    if (NewSwitch != NewHead)
      AddIncoming(D.Switch, NewSwitch);

    for (unsigned I = 0; I != Defs.size(); ++I) {
      Value *V = VMap.lookup(Defs[I]);
      BasicBlock *BB = Defs[I]->getParent() == D.Head ? NewHead : NewSwitch;
      Copies[I].push_back({BB, V});
    }

    if (From == D.Gather) {
      for (PHINode &PN : D.Gather->phis())
        PN.removeIncomingValue(Entry, /*DeletePHIIfEmpty=*/false);
    } else {
      for (PHINode &PN : D.Head->phis())
        PN.removeIncomingValue(Entry, /*DeletePHIIfEmpty=*/false);
    }
    Entry->getTerminator()->replaceSuccessorWith(From, NewHead);
  }

  // Each value the region defines now has a copy in every replica; the uses
  // outside the region, and in the side block and the original switch that
  // the replicas also reach, take whichever one arrives.  The copies use only
  // copies, so what still uses the original is what needs rewriting.
  for (unsigned I = 0; I != Defs.size(); ++I) {
    SmallVector<Use *, 8> Uses;
    for (Use &U : Defs[I]->uses()) {
      auto *UI = cast<Instruction>(U.getUser());
      if (UI->getParent() == Defs[I]->getParent() && !isa<PHINode>(UI))
        continue;
      Uses.push_back(&U);
    }
    if (Uses.empty())
      continue;
    SSAUpdater SSA;
    SSA.Initialize(Defs[I]->getType(), Defs[I]->getName());
    SSA.AddAvailableValue(Defs[I]->getParent(), Defs[I]);
    for (auto &[BB, V] : Copies[I])
      SSA.AddAvailableValue(BB, V);
    for (Use *U : Uses)
      SSA.RewriteUse(*U);
  }
}

bool IA64ReplicateDispatch::runOnFunction(Function &F) {
  if (!EnableReplicateDispatch || skipFunction(F) || F.hasOptSize())
    return false;

  DominatorTree DT(F);
  SmallVector<SwitchInst *, 4> Switches;
  for (BasicBlock &BB : F)
    if (auto *SI = dyn_cast<SwitchInst>(BB.getTerminator())) {
      SmallPtrSet<BasicBlock *, 32> Dests(llvm::from_range, successors(&BB));
      if (Dests.size() >= MinDispatchTargets)
        Switches.push_back(SI);
    }

  bool Changed = false;
  for (SwitchInst *SI : Switches) {
    Dispatch D;
    if (!findDispatch(SI, D, DT))
      continue;
    SmallVector<BasicBlock *, 64> Entries;
    findEntries(D, Entries);
    if (Entries.size() < MinDispatchTargets)
      continue;
    replicate(D, Entries);
    DT.recalculate(F);
    Changed = true;
  }
  return Changed;
}
