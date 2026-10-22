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
// operation instead. Loads of a word's bytes in reverse become a load and a
// byte swap, as in a conversion between byte orders. It widens what is left
// to 32-bit words the same way. Where the word is in a local array or a
// global this module defines, it aligns the array to make it so. The wide
// loads issue where the first of the stores was and the wide store where the
// last was, so nothing else between them may touch those bytes; two local
// arrays or globals are told apart even where a loop swaps pointers to them.
// A lane's value that is used after the stores is read back from the word.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-widen-byte-ops"

STATISTIC(NumWidened, "Number of words stored a lane at a time widened");

static cl::opt<unsigned>
    MaxDepth("ia64-widen-byte-ops-max-depth", cl::Hidden, cl::init(4),
             cl::desc("The deepest tree of logical operations widened"));

namespace {

// A pointer as a base and a constant byte offset from it.
struct Address {
  Value *Base = nullptr;
  int64_t Offset = 0;
};

// The loads of a word, a lane each, that a widened tree reads, in the
// order of the lanes they are for. Addr is the word's lowest byte.
struct LoadGroup {
  SmallVector<LoadInst *, 8> Lanes;
  Address Addr;
  // Whether the lanes load the word's bytes in reverse.
  bool Reversed = false;
};

class Widener {
  Function &F;
  const DataLayout &DL;
  AAResults &AA;
  DominatorTree &DT;

  // The size of the word being widened, 8 or 4 bytes.
  int64_t Bytes = 8;
  // The instructions each candidate group would erase.
  SmallPtrSet<Instruction *, 32> Members;
  SmallVector<LoadGroup, 4> Loads;

public:
  Widener(Function &F, AAResults &AA, DominatorTree &DT)
      : F(F), DL(F.getDataLayout()), AA(AA), DT(DT) {}
  bool run();

private:
  std::optional<Address> decompose(Value *Ptr) const;
  bool matchLanes(ArrayRef<Value *> Vals, BasicBlock *BB, Type *Ty,
                  unsigned Depth);
  Value *build(ArrayRef<Value *> Vals, IRBuilder<> &Wide, IRBuilder<> &Early,
               unsigned &NextLoad);
  bool accessedAligned(Value *Base, Instruction *At) const;
  bool alignTo(Value *Base, int64_t Offset, Instruction *At);
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
// and xor in BB, over constants and over loads of the lanes of a word.
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
    // The lanes load the bytes of a word in order or, for byte lanes, in
    // reverse.
    LoadGroup G;
    int64_t Width = Ty->getIntegerBitWidth() / 8, Last = Bytes - Width;
    for (auto [K, V] : enumerate(Vals)) {
      auto *LI = dyn_cast<LoadInst>(V);
      if (!LI || !LI->isSimple())
        return false;
      std::optional<Address> A = decompose(LI->getPointerOperand());
      if (!A)
        return false;
      int64_t Pos = K * Width;
      if (K == 0) {
        G.Addr = *A;
      } else if (K == 1 && A->Base == G.Addr.Base &&
                 A->Offset == G.Addr.Offset - 1) {
        G.Reversed = true;
        G.Addr.Offset -= Last;
      }
      if (A->Base != G.Addr.Base ||
          A->Offset != G.Addr.Offset + (G.Reversed ? Last - Pos : Pos))
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

// The word-sized value of the tree matchLanes matched for Vals: its operations
// with Wide and its loads with Early, in the order matchLanes found them.
Value *Widener::build(ArrayRef<Value *> Vals, IRBuilder<> &Wide,
                      IRBuilder<> &Early, unsigned &NextLoad) {
  Type *WordTy = Wide.getIntNTy(Bytes * 8);
  if (all_of(Vals, [&](Value *V) { return isa<ConstantInt>(V); })) {
    unsigned Bits = Vals[0]->getType()->getIntegerBitWidth();
    APInt C(Bytes * 8, 0);
    for (auto [K, V] : enumerate(Vals)) {
      unsigned Lane = DL.isLittleEndian() ? K : Vals.size() - 1 - K;
      C.insertBits(cast<ConstantInt>(V)->getValue(), Lane * Bits);
    }
    return ConstantInt::get(WordTy, C);
  }
  if (isa<LoadInst>(Vals[0])) {
    LoadGroup &G = Loads[NextLoad++];
    Value *Ptr =
        G.Addr.Offset
            ? Early.CreatePtrAdd(G.Addr.Base, Early.getInt64(G.Addr.Offset))
            : G.Addr.Base;
    LoadInst *L = Early.CreateAlignedLoad(WordTy, Ptr, Align(Bytes));
    AAMDNodes AAInfo = G.Lanes[0]->getAAMetadata();
    for (LoadInst *LI : drop_begin(G.Lanes))
      AAInfo = AAInfo.merge(LI->getAAMetadata());
    L->setAAMetadata(AAInfo);
    // Byte order is the reverse of the lanes' on either endianness.
    if (G.Reversed)
      return Wide.CreateUnaryIntrinsic(Intrinsic::bswap, L);
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

// Whether an access through Base or a constant offset from it that runs
// before At shows Base to be aligned to a word, as one of the struct a
// pointer argument points to.
bool Widener::accessedAligned(Value *Base, Instruction *At) const {
  SmallVector<std::pair<Value *, int64_t>, 8> Ptrs{{Base, 0}};
  for (User *U : Base->users())
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      APInt Off(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      if (GEP->getPointerOperand() == Base &&
          GEP->accumulateConstantOffset(DL, Off) &&
          Off.getSignificantBits() <= 64)
        Ptrs.push_back({GEP, Off.getSExtValue()});
    }
  for (auto [Ptr, Off] : Ptrs)
    for (User *U : Ptr->users()) {
      auto *I = dyn_cast<Instruction>(U);
      if (!I || !DT.dominates(I, At))
        continue;
      MaybeAlign A;
      if (getLoadStorePointerOperand(I) == Ptr)
        A = getLoadStoreAlignment(I);
      else if (auto *MI = dyn_cast<MemIntrinsic>(I);
               MI && MI->getRawDest() == Ptr)
        A = MI->getDestAlign();
      if (A && commonAlignment(*A, Off) >= Align(Bytes))
        return true;
    }
  return false;
}

// Whether Base + Offset is aligned to a word, aligning the local arrays
// and globals it may point into if need be. Other pointers it may be or
// step from must be accessed aligned before At.
bool Widener::alignTo(Value *Base, int64_t Offset, Instruction *At) {
  if (Offset % Bytes)
    return false;
  // A pointer may be one of several arrays, as when a loop swaps two
  // buffers, or step through one.
  SmallVector<Value *, 4> Worklist{Base};
  SmallPtrSet<Value *, 4> Seen{Base};
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (getOrEnforceKnownAlignment(V, Align(Bytes), DL) >= Align(Bytes))
      continue;
    SmallVector<Value *, 4> Incoming;
    std::optional<Address> A = decompose(V);
    if (A && A->Base != V) {
      if (A->Offset % Bytes)
        return false;
      Incoming = {A->Base};
    } else if (auto *PN = dyn_cast<PHINode>(V))
      append_range(Incoming, PN->incoming_values());
    else if (auto *SI = dyn_cast<SelectInst>(V))
      Incoming = {SI->getTrueValue(), SI->getFalseValue()};
    else if (!accessedAligned(V, At))
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

  // The words loaded and stored, from the lowest byte's pointer.
  auto Wide = [&](Value *Ptr) {
    return MemoryLocation(Ptr, LocationSize::precise(Bytes));
  };
  auto Low = [&](auto *I) { return I->getPointerOperand(); };
  MemoryLocation StoreLoc = Wide(Low(Stores.front()));
  SmallVector<MemoryLocation, 4> LoadLocs;
  for (LoadGroup &G : Loads) {
    MemoryLocation L = Wide(Low(G.Reversed ? G.Lanes.back() : G.Lanes.front()));
    // Loads of the stored word itself must each read its bytes before
    // their store writes them.
    if (G.Addr.Base == StoreAddr.Base && G.Addr.Offset == StoreAddr.Offset) {
      unsigned N = G.Lanes.size();
      for (auto [K, LI] : enumerate(G.Lanes))
        if (!LI->comesBefore(Stores[G.Reversed ? N - 1 - K : K]))
          return false;
      LoadLocs.push_back(L);
      continue;
    }
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

// Widens Stores, the lanes of the word at Addr in order, if their
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
  if (!matchLanes(Vals, BB, Ty, 0)) {
    LLVM_DEBUG(dbgs() << "not one tree\n");
    return false;
  }
  // Stores of constants and copies are for store merging to combine.
  if (!isa<BinaryOperator>(Vals[0]) &&
      none_of(Loads, [](const LoadGroup &G) { return G.Reversed; })) {
    LLVM_DEBUG(dbgs() << "a copy\n");
    return false;
  }
  // Other users of a lane's value read it from the word, so must come
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
  Instruction *First = Stores[0];
  for (Instruction *I : Members)
    if (I->comesBefore(First))
      First = I;
  for (StoreInst *SI : Stores)
    if (SI->comesBefore(First))
      First = SI;
  if (!alignTo(Addr.Base, Addr.Offset, First) ||
      !all_of(Loads, [&](const LoadGroup &G) {
        return alignTo(G.Addr.Base, G.Addr.Offset, First);
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
  StoreInst *St = Wide.CreateAlignedStore(V, Ptr, Align(Bytes));
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
    // The stores of each width to each base, by offset; a word with
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

    // Doublewords first, then words of what is left.
    for (int64_t WordBytes : {8, 4}) {
      Bytes = WordBytes;
      for (auto &[Key, Offsets] : ByBase) {
        auto [Base, Ty] = Key;
        int64_t Width = Ty->getIntegerBitWidth() / 8;
        if (Width >= Bytes)
          continue;
        SmallVector<int64_t, 16> Starts;
        for (auto &[Off, SI] : Offsets)
          if (Off % Bytes == 0)
            Starts.push_back(Off);
        llvm::sort(Starts);
        for (int64_t Start : Starts) {
          SmallVector<StoreInst *, 8> Stores;
          for (int64_t Off = Start; Off < Start + Bytes; Off += Width) {
            auto It = Offsets.find(Off);
            if (It == Offsets.end() || Ambiguous.contains(It->second))
              break;
            Stores.push_back(It->second);
          }
          if (Stores.size() * Width == Bytes &&
              widen(Stores, Address{Base, Start})) {
            for (int64_t Off = Start; Off < Start + Bytes; Off += Width)
              Offsets.erase(Off);
            Changed = true;
          }
        }
      }
    }
  }
  return Changed;
}

PreservedAnalyses IA64WidenByteOpsPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  if (!Widener(F, AM.getResult<AAManager>(F),
               AM.getResult<DominatorTreeAnalysis>(F))
           .run())
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
