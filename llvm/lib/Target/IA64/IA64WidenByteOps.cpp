//===-- IA64WidenByteOps.cpp - Do bytewise logic a doubleword at a time ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An unrolled loop like
//
//   for (i = 0; i < 16; ++i)
//     out[i] ^= in[i];
//
// stores to each byte of a doubleword in turn. On the Itanium 9340 a 1- or
// 2-byte store to a doubleword that the store ahead of it also wrote leaves
// the store queue 6 cycles after that one, and a load of a doubleword that
// a queued byte store writes waits for it to leave, so the loop above costs
// about 84 cycles where two ld8, xor and st8 per doubleword take a few.
// Without vector types, nothing else turns the bytes back into doublewords.
//
// This pass finds stores to every lane of an aligned doubleword whose values
// are the same tree of and, or and xor over loads of whole aligned
// doublewords, a lane of each, and over constants, and does them as one i64
// operation instead. Where the doubleword is in a local array or a global
// this module defines, it aligns the array to make it so. The wide loads
// issue where the first of the stores was and the wide store where the last
// was, so nothing else between them may touch those bytes; two local arrays
// or globals are told apart even where a loop swaps pointers to them. A
// lane's value that is used after the stores is read back from the i64.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-widen-byte-ops"

STATISTIC(NumWidened, "Number of doublewords stored a lane at a time widened");

static cl::opt<unsigned>
    MaxDepth("ia64-widen-byte-ops-max-depth", cl::Hidden, cl::init(4),
             cl::desc("The deepest tree of logical operations widened"));

namespace {

constexpr unsigned WideBytes = 8;

// A pointer as a base and a constant byte offset from it.
struct Address {
  Value *Base = nullptr;
  int64_t Offset = 0;
};

// The loads of a doubleword, a lane each, that a widened tree reads.
struct LoadGroup {
  SmallVector<LoadInst *, 8> Lanes;
  Address Addr;
};

class Widener {
  Function &F;
  const DataLayout &DL;
  AAResults &AA;

  // The instructions each candidate group would erase.
  SmallPtrSet<Instruction *, 32> Members;
  SmallVector<LoadGroup, 4> Loads;

public:
  Widener(Function &F, AAResults &AA) : F(F), DL(F.getDataLayout()), AA(AA) {}
  bool run();

private:
  std::optional<Address> decompose(Value *Ptr) const;
  bool matchLanes(ArrayRef<Value *> Vals, BasicBlock *BB, Type *Ty,
                  unsigned Depth);
  Value *build(ArrayRef<Value *> Vals, IRBuilder<> &Wide, IRBuilder<> &Early,
               unsigned &NextLoad);
  bool alignTo(Value *Base, int64_t Offset);
  bool safeToMove(ArrayRef<StoreInst *> Stores, const Address &StoreAddr);
  bool widen(ArrayRef<StoreInst *> Stores, const Address &Addr);
};

} // end anonymous namespace

std::optional<Address> Widener::decompose(Value *Ptr) const {
  if (Ptr->getType()->getPointerAddressSpace() != 0)
    return std::nullopt;
  APInt Off(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
  Value *Base =
      Ptr->stripAndAccumulateConstantOffsets(DL, Off,
                                             /*AllowNonInbounds=*/true);
  if (Off.getSignificantBits() > 64)
    return std::nullopt;
  return Address{Base, Off.getSExtValue()};
}

// Whether Vals, the value of each lane in turn, are the same tree of and, or
// and xor in BB, over constants and over loads of the lanes of a doubleword.
// Adds what they would erase to Members and their loads to Loads. Only the
// lanes' own values may have uses outside the tree.
bool Widener::matchLanes(ArrayRef<Value *> Vals, BasicBlock *BB, Type *Ty,
                         unsigned Depth) {
  if (all_of(Vals, [&](Value *V) { return isa<ConstantInt>(V); }))
    return true;

  auto Local = [&](Value *V) {
    auto *I = dyn_cast<Instruction>(V);
    return I && I->getParent() == BB && I->getType() == Ty &&
           (Depth == 0 || I->hasOneUse()) && !Members.contains(I);
  };
  if (!all_of(Vals, Local))
    return false;

  if (isa<LoadInst>(Vals[0])) {
    LoadGroup G;
    for (auto [K, V] : enumerate(Vals)) {
      auto *LI = dyn_cast<LoadInst>(V);
      if (!LI || !LI->isSimple())
        return false;
      std::optional<Address> A = decompose(LI->getPointerOperand());
      if (!A)
        return false;
      if (K == 0)
        G.Addr = *A;
      else if (A->Base != G.Addr.Base ||
               A->Offset !=
                   G.Addr.Offset + int64_t(K * Ty->getIntegerBitWidth() / 8))
        return false;
      G.Lanes.push_back(LI);
    }
    for (LoadInst *LI : G.Lanes)
      Members.insert(LI);
    Loads.push_back(std::move(G));
    return true;
  }

  if (Depth >= MaxDepth)
    return false;
  auto *Op0 = dyn_cast<BinaryOperator>(Vals[0]);
  if (!Op0 || (Op0->getOpcode() != Instruction::And &&
               Op0->getOpcode() != Instruction::Or &&
               Op0->getOpcode() != Instruction::Xor))
    return false;
  SmallVector<Value *, 8> LHS, RHS;
  for (Value *V : Vals) {
    auto *BO = dyn_cast<BinaryOperator>(V);
    if (!BO || BO->getOpcode() != Op0->getOpcode())
      return false;
    LHS.push_back(BO->getOperand(0));
    RHS.push_back(BO->getOperand(1));
    Members.insert(BO);
  }
  return matchLanes(LHS, BB, Ty, Depth + 1) &&
         matchLanes(RHS, BB, Ty, Depth + 1);
}

// The i64 value of the tree matchLanes matched for Vals: its operations with
// Wide and its loads with Early, in the order matchLanes found them.
Value *Widener::build(ArrayRef<Value *> Vals, IRBuilder<> &Wide,
                      IRBuilder<> &Early, unsigned &NextLoad) {
  Type *I64 = Wide.getInt64Ty();
  if (all_of(Vals, [&](Value *V) { return isa<ConstantInt>(V); })) {
    unsigned Bits = Vals[0]->getType()->getIntegerBitWidth();
    APInt C(64, 0);
    for (auto [K, V] : enumerate(Vals)) {
      unsigned Lane = DL.isLittleEndian() ? K : Vals.size() - 1 - K;
      C.insertBits(cast<ConstantInt>(V)->getValue(), Lane * Bits);
    }
    return ConstantInt::get(I64, C);
  }
  if (isa<LoadInst>(Vals[0])) {
    LoadGroup &G = Loads[NextLoad++];
    Value *Ptr =
        G.Addr.Offset
            ? Early.CreatePtrAdd(G.Addr.Base, Early.getInt64(G.Addr.Offset))
            : G.Addr.Base;
    LoadInst *L = Early.CreateAlignedLoad(I64, Ptr, Align(WideBytes));
    AAMDNodes AAInfo = G.Lanes[0]->getAAMetadata();
    for (LoadInst *LI : drop_begin(G.Lanes))
      AAInfo = AAInfo.merge(LI->getAAMetadata());
    L->setAAMetadata(AAInfo);
    return L;
  }
  auto *Op0 = cast<BinaryOperator>(Vals[0]);
  SmallVector<Value *, 8> LHS, RHS;
  for (Value *V : Vals) {
    LHS.push_back(cast<BinaryOperator>(V)->getOperand(0));
    RHS.push_back(cast<BinaryOperator>(V)->getOperand(1));
  }
  Value *L = build(LHS, Wide, Early, NextLoad);
  Value *R = build(RHS, Wide, Early, NextLoad);
  return Wide.CreateBinOp(Op0->getOpcode(), L, R);
}

// Whether Base + Offset is aligned to a doubleword, aligning the local arrays
// and globals it may point into if need be.
bool Widener::alignTo(Value *Base, int64_t Offset) {
  if (Offset % WideBytes)
    return false;
  // A pointer may be one of several arrays, as when a loop swaps two
  // buffers.
  SmallVector<Value *, 4> Worklist{Base};
  SmallPtrSet<Value *, 4> Seen{Base};
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (getOrEnforceKnownAlignment(V, Align(WideBytes), DL) >= Align(WideBytes))
      continue;
    SmallVector<Value *, 4> Incoming;
    if (auto *PN = dyn_cast<PHINode>(V))
      append_range(Incoming, PN->incoming_values());
    else if (auto *SI = dyn_cast<SelectInst>(V))
      Incoming = {SI->getTrueValue(), SI->getFalseValue()};
    else
      return false;
    for (Value *In : Incoming)
      if (Seen.insert(In).second)
        Worklist.push_back(In);
  }
  return true;
}

// Whether A and B point into different local arrays or globals, where alias
// analysis cannot tell, as of two pointers a loop swaps.
static bool distinctObjects(Value *A, Value *B) {
  SmallVector<const Value *, 4> ObjsA, ObjsB;
  getUnderlyingObjects(A, ObjsA);
  getUnderlyingObjects(B, ObjsB);
  auto Known = [](const Value *V) {
    return isa<AllocaInst>(V) || isa<GlobalVariable>(V);
  };
  return all_of(ObjsA, Known) && all_of(ObjsB, Known) &&
         none_of(ObjsA, [&](const Value *V) { return is_contained(ObjsB, V); });
}

// Whether the lanes' loads can all move to the first of Stores and the
// stores to the last: whether nothing else between the first member and the
// last store writes what they load or touches what they store, and the
// stores write nothing the loads read but their own lanes.
bool Widener::safeToMove(ArrayRef<StoreInst *> Stores,
                         const Address &StoreAddr) {
  StoreInst *LastStore = Stores[0];
  for (StoreInst *SI : Stores)
    if (LastStore->comesBefore(SI))
      LastStore = SI;
  Instruction *First = LastStore;
  for (Instruction *I : Members)
    if (I->comesBefore(First))
      First = I;
  for (StoreInst *SI : Stores)
    if (SI->comesBefore(First))
      First = SI;

  // The doublewords loaded and stored, from the lowest lane's pointer.
  auto Wide = [&](Value *Ptr) {
    return MemoryLocation(Ptr, LocationSize::precise(WideBytes));
  };
  auto LowLane = [&](ArrayRef<StoreInst *> S) {
    for (StoreInst *SI : S)
      if (decompose(SI->getPointerOperand())->Offset == StoreAddr.Offset)
        return SI->getPointerOperand();
    llvm_unreachable("no store to the lowest lane");
  };
  MemoryLocation StoreLoc = Wide(LowLane(Stores));
  SmallVector<MemoryLocation, 4> LoadLocs;
  for (LoadGroup &G : Loads) {
    // Each lane loads before its own store, and other lanes' stores write
    // other bytes of the same doubleword.
    if (G.Addr.Base == StoreAddr.Base && G.Addr.Offset == StoreAddr.Offset) {
      LoadLocs.push_back(Wide(G.Lanes[0]->getPointerOperand()));
      continue;
    }
    MemoryLocation L = Wide(G.Lanes[0]->getPointerOperand());
    if (!AA.isNoAlias(L, StoreLoc) &&
        !distinctObjects(G.Addr.Base, StoreAddr.Base))
      return false;
    LoadLocs.push_back(L);
  }

  // Each base must be available where the loads move to.
  Instruction *FirstStore = LastStore;
  for (StoreInst *SI : Stores)
    if (SI->comesBefore(FirstStore))
      FirstStore = SI;
  for (LoadGroup &G : Loads)
    if (auto *I = dyn_cast<Instruction>(G.Addr.Base))
      if (I->getParent() == FirstStore->getParent() &&
          !I->comesBefore(FirstStore))
        return false;

  SmallPtrSet<Instruction *, 8> StoreSet(llvm::from_range, Stores);
  for (Instruction *I = First; I != LastStore; I = I->getNextNode()) {
    if (Members.contains(I) || StoreSet.contains(I) ||
        !I->mayReadOrWriteMemory())
      continue;
    if (isModOrRefSet(AA.getModRefInfo(I, StoreLoc)))
      return false;
    for (const MemoryLocation &L : LoadLocs)
      if (isModSet(AA.getModRefInfo(I, L)))
        return false;
  }
  return true;
}

// Widens Stores, the lanes of the doubleword at Addr in order, if their
// values are a tree it can widen.
bool Widener::widen(ArrayRef<StoreInst *> Stores, const Address &Addr) {
  Members.clear();
  Loads.clear();
  BasicBlock *BB = Stores[0]->getParent();
  Type *Ty = Stores[0]->getValueOperand()->getType();
  SmallVector<Value *, 8> Vals;
  for (StoreInst *SI : Stores)
    Vals.push_back(SI->getValueOperand());
  LLVM_DEBUG(dbgs() << "Lanes of " << *Addr.Base << " + " << Addr.Offset
                    << ": ");
  // Stores of constants and copies are for store merging to combine.
  if (!isa<BinaryOperator>(Vals[0]) || !matchLanes(Vals, BB, Ty, 0)) {
    LLVM_DEBUG(dbgs() << "not one tree\n");
    return false;
  }
  // Other users of a lane's value read it from the doubleword, so must come
  // after it.
  StoreInst *LastStore = Stores[0];
  for (StoreInst *SI : Stores)
    if (LastStore->comesBefore(SI))
      LastStore = SI;
  SmallVector<std::pair<Use *, unsigned>, 4> Extracts;
  for (auto [K, SI] : enumerate(Stores)) {
    if (isa<Constant>(SI->getValueOperand()))
      continue;
    for (Use &U : SI->getValueOperand()->uses()) {
      auto *I = cast<Instruction>(U.getUser());
      if (I == SI)
        continue;
      if (!isa<PHINode>(I) && I->getParent() == BB &&
          !LastStore->comesBefore(I)) {
        LLVM_DEBUG(dbgs() << "used early\n");
        return false;
      }
      Extracts.push_back({&U, K});
    }
  }
  if (!safeToMove(Stores, Addr)) {
    LLVM_DEBUG(dbgs() << "cannot move\n");
    return false;
  }
  if (!alignTo(Addr.Base, Addr.Offset) ||
      !all_of(Loads, [&](const LoadGroup &G) {
        return alignTo(G.Addr.Base, G.Addr.Offset);
      })) {
    LLVM_DEBUG(dbgs() << "not aligned\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << "widened\n");

  StoreInst *FirstStore = Stores[0];
  for (StoreInst *SI : Stores)
    if (SI->comesBefore(FirstStore))
      FirstStore = SI;
  IRBuilder<> Early(FirstStore), Wide(LastStore);
  unsigned NextLoad = 0;
  Value *V = build(Vals, Wide, Early, NextLoad);
  Value *Ptr = Addr.Offset
                   ? Wide.CreatePtrAdd(Addr.Base, Wide.getInt64(Addr.Offset))
                   : Addr.Base;
  StoreInst *St = Wide.CreateAlignedStore(V, Ptr, Align(WideBytes));
  AAMDNodes AAInfo = Stores[0]->getAAMetadata();
  for (StoreInst *SI : drop_begin(Stores))
    AAInfo = AAInfo.merge(SI->getAAMetadata());
  St->setAAMetadata(AAInfo);
  unsigned Bits = Ty->getIntegerBitWidth();
  for (auto [U, K] : Extracts) {
    unsigned Lane = DL.isLittleEndian() ? K : Stores.size() - 1 - K;
    U->set(Wide.CreateTrunc(Lane ? Wide.CreateLShr(V, Lane * Bits) : V, Ty));
  }

  SmallVector<WeakTrackingVH, 32> Addrs;
  for (StoreInst *SI : Stores) {
    Addrs.push_back(SI->getPointerOperand());
    SI->eraseFromParent();
  }
  for (const LoadGroup &G : Loads)
    for (LoadInst *LI : G.Lanes)
      Addrs.push_back(LI->getPointerOperand());
  // Each member's one use is a member erased before it, users first.
  SmallVector<Instruction *, 32> Dead(Members.begin(), Members.end());
  while (!Dead.empty()) {
    auto It = find_if(Dead, [](Instruction *I) { return I->use_empty(); });
    assert(It != Dead.end() && "a member is used outside the tree");
    (*It)->eraseFromParent();
    Dead.erase(It);
  }
  RecursivelyDeleteTriviallyDeadInstructionsPermissive(Addrs);
  ++NumWidened;
  return true;
}

bool Widener::run() {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    // The stores of each width to each base, by offset; a doubleword with
    // two stores to one lane is not a candidate.
    DenseMap<std::pair<Value *, Type *>, DenseMap<int64_t, StoreInst *>> ByBase;
    SmallPtrSet<StoreInst *, 8> Ambiguous;
    for (Instruction &I : BB) {
      auto *SI = dyn_cast<StoreInst>(&I);
      if (!SI || !SI->isSimple())
        continue;
      Type *Ty = SI->getValueOperand()->getType();
      if (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(16) && !Ty->isIntegerTy(32))
        continue;
      std::optional<Address> A = decompose(SI->getPointerOperand());
      if (!A)
        continue;
      auto [It, New] = ByBase[{A->Base, Ty}].try_emplace(A->Offset, SI);
      if (!New)
        Ambiguous.insert(It->second);
    }

    for (auto &[Key, Offsets] : ByBase) {
      auto [Base, Ty] = Key;
      int64_t Width = Ty->getIntegerBitWidth() / 8;
      SmallVector<int64_t, 16> Starts;
      for (auto &[Off, SI] : Offsets)
        if (Off % int64_t(WideBytes) == 0)
          Starts.push_back(Off);
      llvm::sort(Starts);
      for (int64_t Start : Starts) {
        SmallVector<StoreInst *, 8> Stores;
        for (int64_t Off = Start; Off < Start + int64_t(WideBytes);
             Off += Width) {
          auto It = Offsets.find(Off);
          if (It == Offsets.end() || Ambiguous.contains(It->second))
            break;
          Stores.push_back(It->second);
        }
        if (Stores.size() * Width == WideBytes &&
            widen(Stores, Address{Base, Start})) {
          for (int64_t Off = Start; Off < Start + int64_t(WideBytes);
               Off += Width)
            Offsets.erase(Off);
          Changed = true;
        }
      }
    }
  }
  return Changed;
}

PreservedAnalyses IA64WidenByteOpsPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  if (!Widener(F, AM.getResult<AAManager>(F)).run())
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
