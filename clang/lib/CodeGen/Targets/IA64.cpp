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
  struct Members {
    const Type *Base = nullptr;
    bool HasComplex = false;
  };
  bool isOfOneFloatType(QualType Ty, Members &M, bool AsPassed) const;
  llvm::ArrayType *getMembersType(QualType Ty, const Type *Base) const;
  llvm::ArrayType *getHomogeneousType(QualType Ty) const;
  llvm::ArrayType *getSingleMemberType(QualType Ty) const;
};

class IA64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  IA64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<IA64ABIInfo>(CGT)) {}
};
} // end anonymous namespace

// HFA members can be single, double or extended precision. A quad-precision
// value is always passed as two words in general registers.
static bool isFloatMemberType(const Type *Ty) {
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

// Whether Ty consists entirely of a single floating-point type, which is
// recorded in M. A zero-length array of that type still qualifies, but a
// member of any other type disqualifies it even if zero-sized: an empty
// struct in C, a zero-length integer array, a zero-width bit-field. Things
// C++ gives no storage (an empty base class, a [[no_unique_address]] member)
// are not members.
//
// With AsPassed, instead check whether Ty is of that type ignoring zero-sized
// members, and is neither a union nor ends in a flexible array member.
bool IA64ABIInfo::isOfOneFloatType(QualType Ty, Members &M,
                                   bool AsPassed) const {
  if (const ArrayType *AT = getContext().getAsArrayType(Ty))
    return isOfOneFloatType(AT->getElementType(), M, AsPassed);
  if (const auto *CT = Ty->getAs<ComplexType>()) {
    M.HasComplex = true;
    return isOfOneFloatType(CT->getElementType(), M, AsPassed);
  }

  if (const RecordDecl *RD = Ty->getAsRecordDecl()) {
    if (AsPassed && (RD->isUnion() || RD->hasFlexibleArrayMember()))
      return false;
    bool HasMember = false;
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        if (isEmptyRecord(getContext(), B.getType(), true))
          continue;
        if (!isOfOneFloatType(B.getType(), M, AsPassed))
          return false;
        HasMember = true;
      }
    }
    for (const FieldDecl *FD : RD->fields()) {
      if (AsPassed && (FD->isZeroLengthBitField() ||
                       getContext().getTypeSize(FD->getType()) == 0))
        continue;
      if (FD->isBitField())
        return false;
      if (FD->isZeroSize(getContext()))
        continue;
      if (!isOfOneFloatType(FD->getType(), M, AsPassed))
        return false;
      HasMember = true;
    }
    return HasMember;
  }

  const Type *T = Ty.getCanonicalType().getTypePtr();
  if (!isFloatMemberType(T))
    return false;
  if (!M.Base)
    M.Base = T;
  return M.Base == T;
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

// An array of Base filling Ty. Padding in an aggregate declared with greater
// alignment than its members is counted as members and passed in registers
// like them.
llvm::ArrayType *IA64ABIInfo::getMembersType(QualType Ty,
                                             const Type *Base) const {
  uint64_t Bits = getContext().getTypeSize(Ty);
  uint64_t BaseBits = getContext().getTypeSize(Base);
  if (Bits == 0 || Bits % BaseBits != 0)
    return nullptr;
  return llvm::ArrayType::get(CGT.ConvertType(QualType(Base, 0)),
                              Bits / BaseBits);
}

// The member array that makes Ty an HFA, or null if it is not one.
llvm::ArrayType *IA64ABIInfo::getHomogeneousType(QualType Ty) const {
  Members M;
  if (!isOfOneFloatType(Ty, M, /*AsPassed=*/false) || !M.Base)
    return nullptr;
  return getMembersType(Ty, M.Base);
}

// An aggregate that is a single floating-point or complex value plus
// zero-sized members, and so not an HFA, is passed by GCC like that value,
// though not returned that way. Single-precision complex is the exception,
// as is an aggregate declared with less alignment than the value.
llvm::ArrayType *IA64ABIInfo::getSingleMemberType(QualType Ty) const {
  Members M;
  if (!isOfOneFloatType(Ty, M, /*AsPassed=*/true) || !M.Base)
    return nullptr;
  if (getContext().getTypeAlign(Ty) < getContext().getTypeAlign(M.Base))
    return nullptr;
  llvm::ArrayType *Type = getMembersType(Ty, M.Base);
  if (!Type)
    return nullptr;
  if (Type->getNumElements() == 1 && !M.HasComplex)
    return Type;
  if (Type->getNumElements() == 2 && M.HasComplex &&
      !M.Base->isSpecificBuiltinType(BuiltinType::Float))
    return Type;
  return nullptr;
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
  llvm::ArrayType *Members = getHomogeneousType(Ty);
  if (!Members)
    Members = getSingleMemberType(Ty);
  if (Members) {
    // Extended-precision members do not make the aggregate 16-byte aligned
    // if it was declared with less alignment.
    unsigned Align = Aligned ? 16 : 0;
    if (!Aligned && Members->getElementType()->isX86_FP80Ty())
      Align = 8;
    return ABIArgInfo::getDirect(Members, 0, nullptr, true, Align);
  }

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
