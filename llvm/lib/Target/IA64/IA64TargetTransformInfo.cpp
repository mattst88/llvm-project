//===-- IA64TargetTransformInfo.cpp - IA64 specific TTI -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// IA64 target transform information for IR-level passes.
//
//===----------------------------------------------------------------------===//

#include "IA64TargetTransformInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "ia64tti"

static cl::opt<unsigned> UnrollThreshold(
    "ia64-unroll-threshold", cl::Hidden, cl::init(30),
    cl::desc("Size up to which loops are unrolled partially or at runtime"));

// Zero is r0. An add of an immediate to r0 materializes a 22-bit signed
// constant in one slot of a bundle; anything wider needs movl, which takes two.
static InstructionCost materializationCost(int64_t Value) {
  if (Value == 0)
    return TargetTransformInfo::TCC_Free;
  if (isInt<22>(Value))
    return TargetTransformInfo::TCC_Basic;
  return 2 * TargetTransformInfo::TCC_Basic;
}

InstructionCost
IA64TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                           TTI::TargetCostKind CostKind) const {
  assert(Ty->isIntegerTy() && "a constant that is not an integer");
  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return TTI::TCC_Free;

  // A constant wider than 64 bits takes one register per 64-bit chunk.
  APInt Value = Imm.sextOrTrunc(alignTo(BitSize, 64));
  InstructionCost Cost = 0;
  for (unsigned Shift = 0; Shift < Value.getBitWidth(); Shift += 64)
    Cost += materializationCost(Value.extractBitsAsZExtValue(64, Shift));
  return Cost;
}

InstructionCost IA64TTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                               const APInt &Imm, Type *Ty,
                                               TTI::TargetCostKind CostKind,
                                               Instruction *Inst) const {
  assert(Ty->isIntegerTy() && "a constant that is not an integer");
  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0 || Imm.isZero())
    return TTI::TCC_Free;
  if (BitSize > 64)
    return getIntImmCost(Imm, Ty, CostKind);

  int64_t Value = Imm.getSExtValue();
  uint64_t Bits = Imm.getZExtValue();
  switch (Opcode) {
  default:
    break;
  case Instruction::GetElementPtr:
    // A constant address must be materialized in a register; a constant
    // offset folds into an add or a load or store post-increment.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Add:
    if (Idx == 1 && isInt<14>(Value))
      return TTI::TCC_Free;
    break;
  case Instruction::Sub:
    // Subtracting a constant is adding its negation, and a constant minuend
    // fits an 8-bit immediate.
    if (Idx == 1 && isInt<14>(int64_t(0 - Bits)))
      return TTI::TCC_Free;
    if (Idx == 0 && isInt<8>(Value))
      return TTI::TCC_Free;
    break;
  case Instruction::And:
    // A mask that is a contiguous run of ones, or of zeros, becomes an
    // extract, deposit or field clear.
    if (Idx == 1 &&
        (isInt<8>(Value) || isShiftedMask_64(Bits) || isShiftedMask_64(~Bits)))
      return TTI::TCC_Free;
    break;
  case Instruction::Or:
    if (Idx == 1 && (isInt<8>(Value) || isShiftedMask_64(Bits)))
      return TTI::TCC_Free;
    break;
  case Instruction::Xor:
    if (Idx == 1 && isInt<8>(Value))
      return TTI::TCC_Free;
    break;
  case Instruction::ICmp:
    if (Idx == 1 && isInt<8>(Value))
      return TTI::TCC_Free;
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (Idx == 1)
      return TTI::TCC_Free;
    break;
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    // Division by a constant becomes multiplication by another constant,
    // which is impossible once the divisor is hoisted into a register.
    if (Idx == 1)
      return TTI::TCC_Free;
    break;
  case Instruction::Mul:
    // Multiplication by a constant with few bits set is shifts and adds.
    if (Idx == 1 && (Imm.isPowerOf2() || Imm.isNegatedPowerOf2() ||
                     (Imm - 1).isPowerOf2() || (Imm + 1).isPowerOf2()))
      return TTI::TCC_Free;
    break;
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
  case Instruction::BitCast:
    return TTI::TCC_Free;
  }
  return getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost
IA64TTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                 const APInt &Imm, Type *Ty,
                                 TTI::TargetCostKind CostKind) const {
  assert(Ty->isIntegerTy() && "a constant that is not an integer");
  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0 || Imm.isZero())
    return TTI::TCC_Free;
  if (BitSize > 64)
    return getIntImmCost(Imm, Ty, CostKind);

  switch (IID) {
  default:
    break;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
    if (Idx == 1 && isInt<14>(Imm.getSExtValue()))
      return TTI::TCC_Free;
    break;
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
    if (Idx == 1 && isInt<14>(int64_t(0 - Imm.getZExtValue())))
      return TTI::TCC_Free;
    break;
  // These record their operands as constants wherever they appear.
  case Intrinsic::experimental_stackmap:
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
  case Intrinsic::experimental_gc_statepoint:
    return TTI::TCC_Free;
  }
  return getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost IA64TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args, const Instruction *CtxI) const {
  bool Latency = CostKind == TTI::TCK_Latency;
  bool Size = CostKind == TTI::TCK_CodeSize;

  if (Ty->isIntegerTy() && Ty->getIntegerBitWidth() <= 64) {
    bool Signed = Opcode == Instruction::SDiv || Opcode == Instruction::SRem;
    bool Remainder = Opcode == Instruction::URem || Opcode == Instruction::SRem;
    switch (Opcode) {
    default:
      break;
    case Instruction::Mul:
      if (Op2Info.isConstant() && Op2Info.isPowerOf2())
        return TTI::TCC_Basic;
      // Each operand is moved to an FP register, multiplied with xma, and
      // the product moved back. The moves take five cycles and xma four.
      return Latency ? 14 : 4;
    case Instruction::UDiv:
    case Instruction::URem:
    case Instruction::SDiv:
    case Instruction::SRem:
      if (Op2Info.isConstant()) {
        // By a power of two it is a shift or a mask, plus a sign fixup. By
        // another constant it is a multiply-high and one or two shifts, and
        // the remainder takes one more multiply.
        if (Op2Info.isPowerOf2())
          return Signed ? 4 : 1;
        InstructionCost Cost = Signed ? 8 : 6;
        if (Remainder)
          Cost += 5;
        return Latency ? Cost + 10 : Cost;
      }
      // The libcall is two instructions at the call site and a few dozen in
      // the callee.
      return Size ? 2 : Latency ? 40 : 20;
    }
  }

  // Thirteen instructions, mostly fma, with a dependence chain seven deep.
  if (Opcode == Instruction::FDiv && (Ty->isFloatTy() || Ty->isDoubleTy()))
    return Latency ? 30 : 13;

  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CtxI);
}

// Whether a store in the loop may write what one of its loads reads, as far
// as can be told without alias analysis: unless the two access the same
// address, distinct identified objects, or carry the scopes of a loop
// version checked apart.
static bool mayStoreToLoad(const Loop *L) {
  SmallVector<const LoadInst *, 8> Loads;
  SmallVector<const StoreInst *, 8> Stores;
  for (const BasicBlock *BB : L->blocks())
    for (const Instruction &I : *BB) {
      if (auto *Ld = dyn_cast<LoadInst>(&I))
        Loads.push_back(Ld);
      else if (auto *St = dyn_cast<StoreInst>(&I))
        Stores.push_back(St);
    }
  for (const StoreInst *St : Stores)
    for (const LoadInst *Ld : Loads) {
      if (St->getPointerOperand() == Ld->getPointerOperand())
        continue;
      if ((St->hasMetadata(LLVMContext::MD_noalias) &&
           Ld->hasMetadata(LLVMContext::MD_alias_scope)) ||
          (Ld->hasMetadata(LLVMContext::MD_noalias) &&
           St->hasMetadata(LLVMContext::MD_alias_scope)))
        continue;
      const Value *A = getUnderlyingObject(St->getPointerOperand());
      const Value *B = getUnderlyingObject(Ld->getPointerOperand());
      if (A != B && isIdentifiedObject(A) && isIdentifiedObject(B))
        continue;
      return true;
    }
  return false;
}

// The pipeliner overlaps the iterations of a loop of one block whether or
// not it is unrolled, so unrolling it gains only where the units an
// iteration takes leave some idle every cycle (two loads a cycle, two
// stores, four memory ops, two floating-point ops, six instructions) and no
// recurrence through a header phi takes longer than the iteration.  Return
// whether a count up to 8 takes fewer cycles an iteration than one
// iteration alone.  The induction variables and the address arithmetic are
// left out: the pipeliner folds them into post-increments and br.ctop.
static bool runtimeUnrollGains(const Loop *L) {
  BasicBlock *Header = L->getHeader(), *Latch = L->getLoopLatch();
  auto *Br = Latch ? dyn_cast<CondBrInst>(Latch->getTerminator()) : nullptr;
  if (!Br)
    return false;
  SmallPtrSet<const Value *, 16> Overhead;
  Overhead.insert(Br->getCondition());
  for (PHINode &Phi : Header->phis()) {
    auto *Next = dyn_cast<BinaryOperator>(Phi.getIncomingValueForBlock(Latch));
    if (Next &&
        (Next->getOpcode() == Instruction::Add ||
         Next->getOpcode() == Instruction::Sub) &&
        Next->getOperand(0) == &Phi && L->isLoopInvariant(Next->getOperand(1)))
      Overhead.insert(Next);
  }
  for (const Instruction &I : reverse(*Header)) {
    if (isa<GetElementPtrInst>(I) ||
        (!I.mayReadOrWriteMemory() && !I.isTerminator() && !I.use_empty() &&
         all_of(I.users(), [&](const User *U) {
           return Overhead.count(U) ||
                  (isa<PHINode>(U) && cast<PHINode>(U)->getParent() == Header);
         })))
      Overhead.insert(&I);
  }

  unsigned Loads = 0, Stores = 0, Mem = 0, FP = 0, All = 0;
  auto Latency = [](const Instruction &I) -> unsigned {
    if (isa<LoadInst>(I))
      return I.getType()->isFloatingPointTy() ? 6 : 2;
    if (I.getType()->isFloatingPointTy() || isa<FCmpInst>(I) ||
        isa<FPToSIInst>(I) || isa<FPToUIInst>(I))
      return 4;
    if (I.getOpcode() == Instruction::Mul)
      return 8;
    return 1;
  };
  for (const Instruction &I : *Header) {
    if (isa<PHINode>(I) || I.isTerminator() || Overhead.count(&I) ||
        isa<DbgInfoIntrinsic>(I))
      continue;
    ++All;
    if (isa<LoadInst>(I)) {
      ++Loads, ++Mem;
    } else if (isa<StoreInst>(I)) {
      ++Stores, ++Mem;
    } else if (I.getType()->isFPOrFPVectorTy() || isa<FCmpInst>(I)) {
      ++FP;
      // An integer operand moves to the FP registers through a memory unit.
      for (const Value *Op : I.operands())
        if (Op->getType()->isIntegerTy())
          ++Mem, ++All;
    } else if (I.getOpcode() == Instruction::Mul) {
      // setf, xma and getf.
      Mem += 2, ++FP, All += 2;
    } else if (isa<CastInst>(I) &&
               I.getOperand(0)->getType()->isFPOrFPVectorTy()) {
      ++Mem;
    }
  }

  // The longest path from each header phi to its next value, but for an
  // induction variable, which unrolled steps once for several iterations,
  // and an FP sum that may be reassociated.
  unsigned Rec = 0;
  for (PHINode &Phi : Header->phis()) {
    const Value *Next = Phi.getIncomingValueForBlock(Latch);
    auto *FPNext = dyn_cast<FPMathOperator>(Next);
    if (Overhead.count(Next) || (FPNext && FPNext->hasAllowReassoc()))
      continue;
    DenseMap<const Value *, unsigned> Path;
    std::function<unsigned(const Value *)> Longest = [&](const Value *V) {
      if (V == &Phi)
        return 1u;
      auto *I = dyn_cast<Instruction>(V);
      if (!I || I->getParent() != Header || isa<PHINode>(I))
        return 0u;
      auto [It, New] = Path.try_emplace(V, 0);
      if (!New)
        return It->second;
      unsigned Max = 0;
      for (const Value *Op : I->operands())
        Max = std::max(Max, Longest(Op));
      unsigned R = Max ? Max + Latency(*I) : 0;
      Path[V] = R;
      return R;
    };
    if (unsigned P = Longest(Phi.getIncomingValueForBlock(Latch)))
      Rec = std::max(Rec, P - 1);
  }

  auto Cycles = [&](unsigned N) {
    return std::max<uint64_t>({divideCeil(N * Loads, 2),
                               divideCeil(N * Stores, 2),
                               divideCeil(N * Mem, 4), divideCeil(N * FP, 2),
                               divideCeil(N * All, 6), uint64_t(N) * Rec});
  };
  for (unsigned N = 2; N <= 8; N *= 2)
    if (Cycles(N) < N * Cycles(1))
      return true;
  return false;
}

void IA64TTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);

  // A call costs more than the loop overhead that unrolling saves.
  for (BasicBlock *BB : L->blocks())
    for (Instruction &I : *BB)
      if (auto *Call = dyn_cast<CallBase>(&I)) {
        const Function *F = Call->getCalledFunction();
        if (!F || isLoweredToCall(F))
          return;
      }

  UP.Partial = UP.Runtime = UP.UpperBound = true;
  UP.PartialThreshold = UnrollThreshold;
  // A loop of a known trip count that the unroll factor does not divide
  // keeps an exit inside its unrolled body, and the pipeliner only takes a
  // loop of one block.
  if (SE.getSmallConstantTripCount(L))
    UP.AllowRemainder = false;
  // Unrolled at runtime, a loop the pipeliner takes adds a remainder that
  // runs an iteration at a time after the kernel; that costs more than it
  // gains unless the unrolled kernel takes fewer cycles an iteration.  A
  // loop whose stores may write what it loads is overlapped neither way, and
  // unrolling still saves its overhead.
  if (L->getNumBlocks() == 1 && !SE.getSmallConstantTripCount(L) &&
      !mayStoreToLoad(L) && !runtimeUnrollGains(L))
    UP.Runtime = false;

  // A header phi whose next value is another header phi's, as in E = D;
  // D = C, takes a copy each iteration: the two cannot share a register.
  // Unrolled by more than the longest such chain, every phi's next value is
  // computed in the body, and the copies go.  Raise the threshold enough for
  // that, as far as a count dividing the trip count allows.
  if (unsigned TripCount = SE.getSmallConstantTripCount(L)) {
    BasicBlock *Header = L->getHeader(), *Latch = L->getLoopLatch();
    unsigned Chain = 0;
    for (PHINode &Phi : Header->phis()) {
      unsigned N = 0;
      for (PHINode *P = &Phi; Latch && N <= Header->size(); ++N) {
        P = dyn_cast<PHINode>(P->getIncomingValueForBlock(Latch));
        if (!P || P->getParent() != Header)
          break;
      }
      Chain = std::max(Chain, N);
    }
    unsigned Count = Chain + 1;
    while (Count < TripCount && TripCount % Count)
      ++Count;
    if (UnrollThreshold && Chain && Count <= 8 && Count < TripCount) {
      InstructionCost Size = 0;
      for (BasicBlock *BB : L->blocks())
        for (Instruction &I : *BB) {
          SmallVector<const Value *, 4> Operands(I.operand_values());
          Size += getInstructionCost(&I, Operands,
                                     TargetTransformInfo::TCK_CodeSize);
        }
      if (Size.isValid())
        UP.PartialThreshold = std::max<unsigned>(
            UP.PartialThreshold, Size.getValue() * Count + UP.BEInsns);
    }
  }

  // Unrolling cannot shorten an FP reduction that may not be reassociated:
  // each copy still waits for the last.  The pipeliner overlaps the rest of
  // the body without it, and a runtime remainder only adds a serial
  // iteration ahead of the kernel.  A store in the loop can keep the
  // pipeliner from overlapping loads, which unrolling still does.
  for (BasicBlock *BB : L->blocks())
    for (Instruction &I : *BB)
      if (I.mayWriteToMemory())
        return;
  for (PHINode &Phi : L->getHeader()->phis()) {
    RecurrenceDescriptor RD;
    if (Phi.getType()->isFloatingPointTy() &&
        RecurrenceDescriptor::isReductionPHI(&Phi, L, RD, nullptr, nullptr,
                                             nullptr, &SE) &&
        RD.hasExactFPMath()) {
      UP.Runtime = false;
      return;
    }
  }
}

bool IA64TTIImpl::isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                           AssumptionCache &AC,
                                           TargetLibraryInfo *LibInfo,
                                           HardwareLoopInfo &HWLoopInfo) const {
  // Inline assembly may use ar.lc for loops of its own.
  for (BasicBlock *BB : L->blocks())
    for (Instruction &I : *BB)
      if (auto *Call = dyn_cast<CallBase>(&I); Call && Call->isInlineAsm())
        return false;

  LLVMContext &C = L->getHeader()->getContext();
  HWLoopInfo.CountType = Type::getInt64Ty(C);
  HWLoopInfo.LoopDecrement = ConstantInt::get(HWLoopInfo.CountType, 1);
  return true;
}
