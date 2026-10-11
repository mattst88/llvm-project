//===- IA64.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// IA-64 ABI Implementation, following chapter 8 of the Itanium Software
// Conventions and Runtime Architecture Guide.
//
// Each argument occupies one or more 8-byte slots, which map to r32-r39 and
// then to the stack; an aggregate's bytes are laid out in its slots. A
// floating-point argument in one of the first eight slots is passed in f8-f15
// instead, as is each member of an HFA. The backend assigns the slots and
// registers; here it is told how many, as an array of i64, of i128 for a
// 16-byte-aligned argument that starts at an even slot, or of the
// floating-point type.
//===----------------------------------------------------------------------===//

namespace {
class IA64ABIInfo : public DefaultABIInfo {
  // Results of up to four words are returned in r8-r11.
  static const unsigned MaxResultBits = 256;
  static const unsigned MaxResultMembers = 8;
  // Larger arguments are passed to the backend as byval memory rather than
  // as words.
  static const unsigned MaxDirectBits = 16 * 64;

public:
  IA64ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyScalarType(QualType Ty, bool IsResult) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  bool isInSlots(QualType Ty) const;
  llvm::Type *getSlotsType(uint64_t Bits, bool Aligned) const;
  llvm::ArrayType *getHomogeneousType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                         uint64_t Members) const override;
  bool isZeroLengthBitfieldPermittedInHomogeneousAggregate() const override;
};

class IA64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  IA64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<IA64ABIInfo>(CGT)) {}
};
} // end anonymous namespace

// HFA members can be single, double or extended precision. A quad-precision
// value is always passed as two words in general registers.
bool IA64ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  const auto *BT = Ty->getAs<BuiltinType>();
  if (!BT)
    return false;
  switch (BT->getKind()) {
  case BuiltinType::Float:
  case BuiltinType::Double:
  case BuiltinType::LongDouble:
    return true;
  default:
    return false;
  }
}

// An argument has as many members as it likes, and those that f8 to f15 have
// no room for are in the general registers and in memory. A result has no
// more than there are registers, which is for whoever classifies it to see.
bool IA64ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                    uint64_t Members) const {
  return true;
}

// GCC does not overlook a bit-field of no width, in C or in C++.
bool IA64ABIInfo::isZeroLengthBitfieldPermittedInHomogeneousAggregate() const {
  return false;
}

// Types passed in slots as their bytes rather than as a scalar: aggregates,
// complex numbers and vectors (which have no register class of their own).
bool IA64ABIInfo::isInSlots(QualType Ty) const {
  return isAggregateTypeForABI(Ty) || Ty->isAnyComplexType() ||
         Ty->isVectorType();
}

llvm::Type *IA64ABIInfo::getSlotsType(uint64_t Bits, bool Aligned) const {
  llvm::LLVMContext &Ctx = getVMContext();
  if (Bits <= 64)
    return llvm::IntegerType::get(Ctx, llvm::alignTo(Bits, 8));
  unsigned Unit = Aligned ? 128 : 64;
  llvm::Type *UnitTy = llvm::IntegerType::get(Ctx, Unit);
  if (Bits <= Unit)
    return UnitTy;
  return llvm::ArrayType::get(UnitTy, llvm::alignTo(Bits, Unit) / Unit);
}

// The member array that makes Ty an HFA, or null if it is not one.
llvm::ArrayType *IA64ABIInfo::getHomogeneousType(QualType Ty) const {
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (!isHomogeneousAggregate(Ty, Base, Members))
    return nullptr;
  return llvm::ArrayType::get(CGT.ConvertType(QualType(Base, 0)), Members);
}

// Integers narrower than a register are extended by the receiver, not the
// sender: the upper bits are undefined. bool is the exception and is passed
// as a byte holding 0 or 1.
ABIArgInfo IA64ABIInfo::classifyScalarType(QualType Ty, bool IsResult) const {
  if (const auto *ED = Ty->getAsEnumDecl())
    Ty = ED->getIntegerType();

  if (Ty->isBooleanType())
    return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));

  if (Ty->isIntegralOrEnumerationType() && !Ty->isBitIntType())
    return ABIArgInfo::getDirect();

  return IsResult ? DefaultABIInfo::classifyReturnType(Ty)
                  : DefaultABIInfo::classifyArgumentType(Ty);
}

ABIArgInfo IA64ABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (!isInSlots(Ty))
    return classifyScalarType(Ty, /*IsResult=*/false);

  // Types the C++ ABI does not allow to be copied bitwise are passed by the
  // address of a caller-made copy.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   RAA == CGCXXABI::RAA_DirectInMemory);

  // An empty structure is no bytes in C, and has no slot. It is one byte in
  // C++, and has one.
  uint64_t Bits = getContext().getTypeSize(Ty);
  if (Bits == 0)
    return ABIArgInfo::getIgnore();

  // An aggregate declared 16-byte aligned starts at an even slot even if its
  // members do not require that alignment.
  bool Aligned = getContext().getTypeAlign(Ty) > 64;
  if (llvm::Type *Members = getHomogeneousType(Ty))
    return ABIArgInfo::getDirect(Members, 0, nullptr, true, Aligned ? 16 : 0);

  // A large aggregate uses the same slots, but the backend copies it from
  // memory (byval) instead of receiving it a word at a time.
  if (Bits > MaxDirectBits) {
    CharUnits Align = getContext().getTypeAlignInChars(Ty);
    CharUnits SlotAlign = std::clamp(Align, CharUnits::fromQuantity(8),
                                     CharUnits::fromQuantity(16));
    return ABIArgInfo::getIndirect(
        SlotAlign, getDataLayout().getAllocaAddrSpace(),
        /*ByVal=*/true, /*Realign=*/Align > SlotAlign);
  }

  return ABIArgInfo::getDirect(getSlotsType(Bits, Aligned));
}

ABIArgInfo IA64ABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (!isInSlots(RetTy))
    return classifyScalarType(RetTy, /*IsResult=*/true);

  uint64_t Bits = getContext().getTypeSize(RetTy);
  if (Bits == 0)
    return ABIArgInfo::getIgnore();

  // HFAs of up to eight members are returned in f8-f15.
  if (llvm::ArrayType *Members = getHomogeneousType(RetTy))
    if (Members->getNumElements() <= MaxResultMembers)
      return ABIArgInfo::getDirect(Members);

  if (Bits <= MaxResultBits)
    return ABIArgInfo::getDirect(getSlotsType(Bits, /*Aligned=*/false));

  // Anything else is returned in memory whose address the caller passes in
  // r8.
  return getNaturalAlignIndirect(RetTy, getDataLayout().getAllocaAddrSpace(),
                                 /*ByVal=*/false);
}

void IA64ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  // A result the C++ ABI does not allow to be copied bitwise is also returned
  // in memory, but its address is passed as the first argument rather than in
  // r8; inreg tells the backend this.
  if (getCXXABI().classifyReturnType(FI)) {
    if (FI.getReturnInfo().isIndirect())
      FI.getReturnInfo().setInReg(true);
  } else {
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  }
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

// The arguments with no names are one after another in memory, each in the
// slots it was passed in, so one that is aligned to sixteen bytes is at an
// address that is. One of no size was not passed, and has no address to be
// aligned.
RValue IA64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty, AggValueSlot Slot) const {
  bool IsIndirect = isInSlots(Ty) &&
                    getRecordArgABI(Ty, getCXXABI()) != CGCXXABI::RAA_Default;
  TypeInfoChars Info = getContext().getTypeInfoInChars(Ty);
  Info.Align = Info.Width.isZero()
                   ? CharUnits::fromQuantity(8)
                   : std::min(Info.Align, CharUnits::fromQuantity(16));
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, Info,
                          CharUnits::fromQuantity(8),
                          /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createIA64TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<IA64TargetCodeGenInfo>(CGM.getTypes());
}
