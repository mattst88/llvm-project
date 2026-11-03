//===-- IA64SmallRangeMul.cpp - Multiply by a factor of few values --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CPython keeps an integer's sign in two bits of a tag and computes a small
// integer's value as
//
//   (1 - (tag & 3)) * digit
//
// in every arithmetic operation, comparison and hash. IA-64 multiplies integers
// in the FP unit: setf.sig moves each operand over, xma.l multiplies and
// getf.sig brings the product back, some seventeen cycles of latency on
// Itanium 2. But the factor takes only the values -2 to 1.
//
// When one factor S of a multiply is a small constant plus or minus a value
// with at most three unknown bits, like the sign or a string's kind, this pass
// writes the product of X and S as, for the sign,
//
//   X - ((X & -bit0(tag)) + ((X & -bit1(tag)) << 1))
//
// where -bitN is a one-bit signed extr and the shifted add is a shladd, all
// integer ALU work. A multiply by a choice of powers of two, a string's
// character size, becomes a shift by the choice of their logarithms.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "ia64-small-range-mul"

STATISTIC(NumExpanded, "Number of multiplies by a small-range factor expanded");
STATISTIC(NumShifted,
          "Number of multiplies by a choice of powers of two shifted");

namespace {
class IA64SmallRangeMul : public FunctionPass {
public:
  static char ID;
  IA64SmallRangeMul() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "IA64 multiply by a small-range factor";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
};
} // end anonymous namespace

char IA64SmallRangeMul::ID = 0;
INITIALIZE_PASS(IA64SmallRangeMul, DEBUG_TYPE,
                "IA64 multiply by a small-range factor", false, false)

FunctionPass *llvm::createIA64SmallRangeMulPass() {
  return new IA64SmallRangeMul();
}

// A multiply through the FP unit costs some seventeen cycles on Itanium 2;
// three bits, CPython's string kind, cost an extract, an and and an add each.
static cl::opt<unsigned> MaxBits(
    "ia64-small-range-mul-bits", cl::Hidden, cl::init(3),
    cl::desc("The most unknown bits of a factor to expand a multiply by"));

namespace {
// A factor S = C + Y or C - Y, where Y is the constant One plus the bits at
// positions Bits, which may each be clear or set.
struct SmallFactor {
  Value *Y;
  APInt Const; // C + One, or C - One
  bool Neg;    // S = C - Y
  SmallVector<unsigned, 2> Bits;
};
} // end anonymous namespace

static std::optional<SmallFactor> getSmallFactor(Value *S,
                                                 const DataLayout &DL) {
  if (isa<Constant>(S))
    return std::nullopt;
  unsigned W = S->getType()->getIntegerBitWidth();
  SmallFactor F{S, APInt(W, 0), false, {}};
  const APInt *C;
  if (match(S, m_Sub(m_APInt(C), m_Value(F.Y)))) {
    F.Const = *C;
    F.Neg = true;
  } else if (match(S, m_Add(m_Value(F.Y), m_APInt(C)))) {
    F.Const = *C;
  }
  KnownBits K = computeKnownBits(F.Y, DL);
  APInt Unknown = ~(K.Zero | K.One);
  // The sign bit would weigh negatively.
  if (Unknown.popcount() > MaxBits || Unknown.isNegative())
    return std::nullopt;
  for (unsigned Bit = 0; Bit != W; ++Bit)
    if (Unknown[Bit])
      F.Bits.push_back(Bit);
  F.Const = F.Neg ? F.Const - K.One : F.Const + K.One;
  // A small constant is a cheap constant multiply.
  if (F.Const.sgt(8) || F.Const.slt(-8))
    return std::nullopt;
  return F;
}

// Whether S is a phi or a select of powers of two, like the character size
// CPython picks from a string's widest character.
static bool isPow2Choice(Value *S) {
  auto IsPow2 = [](Value *V) {
    auto *C = dyn_cast<ConstantInt>(V);
    return C && C->getValue().isPowerOf2();
  };
  if (auto *Phi = dyn_cast<PHINode>(S))
    return all_of(Phi->incoming_values(), IsPow2);
  if (auto *Sel = dyn_cast<SelectInst>(S))
    return IsPow2(Sel->getTrueValue()) && IsPow2(Sel->getFalseValue());
  return false;
}

// The same choice of the powers' logarithms.
static Value *getLog2Choice(Value *S) {
  auto Log2 = [](Value *V) {
    auto *C = cast<ConstantInt>(V);
    return ConstantInt::get(C->getType(), C->getValue().logBase2());
  };
  if (auto *Phi = dyn_cast<PHINode>(S)) {
    PHINode *L = PHINode::Create(Phi->getType(), Phi->getNumIncomingValues(),
                                 Phi->getName() + ".log2", Phi->getIterator());
    for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I)
      L->addIncoming(Log2(Phi->getIncomingValue(I)), Phi->getIncomingBlock(I));
    return L;
  }
  auto *Sel = cast<SelectInst>(S);
  return SelectInst::Create(Sel->getCondition(), Log2(Sel->getTrueValue()),
                            Log2(Sel->getFalseValue()),
                            Sel->getName() + ".log2", Sel->getIterator());
}

bool IA64SmallRangeMul::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;
  const DataLayout &DL = F.getDataLayout();
  SmallVector<std::tuple<BinaryOperator *, unsigned, SmallFactor>, 8> Muls;
  SmallVector<BinaryOperator *, 8> Shifts;
  for (Instruction &I : instructions(F)) {
    auto *Mul = dyn_cast<BinaryOperator>(&I);
    if (!Mul || Mul->getOpcode() != Instruction::Mul ||
        !Mul->getType()->isIntegerTy() ||
        Mul->getType()->getIntegerBitWidth() > 64 ||
        isa<Constant>(Mul->getOperand(0)) || isa<Constant>(Mul->getOperand(1)))
      continue;
    if (isPow2Choice(Mul->getOperand(0)) || isPow2Choice(Mul->getOperand(1))) {
      Shifts.push_back(Mul);
      continue;
    }
    for (unsigned Op : {1, 0})
      if (auto SF = getSmallFactor(Mul->getOperand(Op), DL)) {
        Muls.emplace_back(Mul, Op, std::move(*SF));
        break;
      }
  }

  for (auto &[Mul, Op, SF] : Muls) {
    Value *X = Mul->getOperand(1 - Op);
    Type *Ty = Mul->getType();
    IRBuilder<> B(Mul);
    // X * (bits of Y): for each bit, X shifted to it if the bit is set.
    Value *Bits = nullptr;
    for (unsigned Bit : SF.Bits) {
      Value *Mask = B.CreateSExt(
          B.CreateTrunc(Bit ? B.CreateLShr(SF.Y, Bit) : SF.Y, B.getInt1Ty()),
          Ty);
      // Masked before the shift, which then folds into shladd.
      Value *Term = B.CreateAnd(X, Mask);
      if (Bit)
        Term = B.CreateShl(Term, Bit);
      Bits = Bits ? B.CreateAdd(Bits, Term) : Term;
    }
    Value *Result;
    if (SF.Const.isZero())
      Result = !Bits    ? Constant::getNullValue(Ty)
               : SF.Neg ? B.CreateNeg(Bits)
                        : Bits;
    else {
      Value *Base = B.CreateMul(X, ConstantInt::get(Ty, SF.Const));
      Result = !Bits    ? Base
               : SF.Neg ? B.CreateSub(Base, Bits)
                        : B.CreateAdd(Base, Bits);
    }
    Result->takeName(Mul);
    Mul->replaceAllUsesWith(Result);
    Mul->eraseFromParent();
    ++NumExpanded;
  }

  // X * a choice of powers of two is X shifted by the choice of their logs.
  for (BinaryOperator *Mul : Shifts) {
    unsigned Op = isPow2Choice(Mul->getOperand(1)) ? 1 : 0;
    Value *Log = getLog2Choice(Mul->getOperand(Op));
    Instruction *Shl = BinaryOperator::CreateShl(Mul->getOperand(1 - Op), Log,
                                                 "", Mul->getIterator());
    Shl->takeName(Mul);
    Mul->replaceAllUsesWith(Shl);
    Mul->eraseFromParent();
    ++NumShifted;
  }
  return !Muls.empty() || !Shifts.empty();
}
