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
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "ia64tti"

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
