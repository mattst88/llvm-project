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
  ABIArgInfo classifyArgumentType(QualType Ty, unsigned &NextSlot) const;

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
  bool isSingleComplexFloat(QualType Ty) const;
  bool isFloatVectorWord(QualType Ty) const;
  llvm::Type *getSignificandsType(unsigned Words) const;
};

class IA64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  IA64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<IA64ABIInfo>(CGT)) {}

  // An unprototyped callee may be variadic, and then expects a floating-point
  // argument somewhere other than where a named one goes. Treating the call
  // as variadic, though it names all its arguments, passes the argument in
  // both places.
  bool isNoProtoCallVariadic(const CallArgList &Args,
                             const FunctionNoProtoType *FnType) const override {
    return true;
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;
    if (FD->hasAttr<IA64SyscallLinkageAttr>())
      cast<llvm::Function>(GV)->addFnAttr("syscall-linkage");
  }
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

// The exception: an aggregate that is one single-precision complex value
// plus zero-sized members. GCC passes its eight bytes as the significand of
// one FP register, and counts two registers for it.
bool IA64ABIInfo::isSingleComplexFloat(QualType Ty) const {
  Members M;
  if (!isOfOneFloatType(Ty, M, /*AsPassed=*/true) || !M.Base)
    return false;
  if (getContext().getTypeAlign(Ty) < getContext().getTypeAlign(M.Base))
    return false;
  return M.HasComplex && M.Base->isSpecificBuiltinType(BuiltinType::Float) &&
         getContext().getTypeSize(Ty) == 64 && !getHomogeneousType(Ty);
}

// A vector of two floats, or a struct that is one plus zero-sized members.
// GCC treats it as a register-sized floating-point value, though not one
// arithmetic is done on, and passes it as the significand of an FP register.
bool IA64ABIInfo::isFloatVectorWord(QualType Ty) const {
  if (getContext().getTypeSize(Ty) != 64)
    return false;
  if (const auto *VT = Ty->getAs<VectorType>())
    return VT->getElementType()->isSpecificBuiltinType(BuiltinType::Float);
  const RecordDecl *RD = Ty->getAsRecordDecl();
  if (!RD || RD->isUnion() || RD->hasFlexibleArrayMember() ||
      getContext().getTypeAlign(Ty) < 64)
    return false;
  unsigned NumMembers = 0;
  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const CXXBaseSpecifier &B : CXXRD->bases()) {
      if (isEmptyRecord(getContext(), B.getType(), true))
        continue;
      if (!isFloatVectorWord(B.getType()))
        return false;
      ++NumMembers;
    }
  }
  for (const FieldDecl *FD : RD->fields()) {
    if (FD->isZeroLengthBitField() || FD->isZeroSize(getContext()) ||
        getContext().getTypeSize(FD->getType()) == 0)
      continue;
    if (FD->isBitField() || !isFloatVectorWord(FD->getType()))
      return false;
    ++NumMembers;
  }
  return NumMembers == 1;
}

// The type that tells the backend to pass each word as the significand of an
// FP register while FP argument registers remain.
llvm::Type *IA64ABIInfo::getSignificandsType(unsigned Words) const {
  llvm::Type *Word = llvm::FixedVectorType::get(
      llvm::Type::getFloatTy(getVMContext()), 2);
  return Words == 1 ? Word : llvm::ArrayType::get(Word, Words);
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

// NextSlot is the slot the argument would start at, and is updated to the
// next argument's. GCC places arguments from the eighth slot on, and those
// with no slot, in ways the type alone does not determine.
ABIArgInfo IA64ABIInfo::classifyArgumentType(QualType Ty,
                                             unsigned &NextSlot) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  uint64_t Bits = getContext().getTypeSize(Ty);
  unsigned Words = llvm::alignTo(Bits, 64) / 64;
  bool Aligned = getContext().getTypeAlign(Ty) > 64;

  if (!isInSlots(Ty)) {
    // A two-word scalar starts at an even slot.
    NextSlot = llvm::alignTo(NextSlot, Words > 1 ? 2 : 1) + Words;
    return classifyScalarType(Ty, /*IsResult=*/false);
  }

  // Types the C++ ABI does not allow to be copied bitwise are passed by the
  // address of a caller-made copy.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    ++NextSlot;
    return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                   RAA == CGCXXABI::RAA_DirectInMemory);
  }

  // An aggregate declared 16-byte aligned starts at an even slot even if its
  // members do not require that alignment.
  bool Skips = Aligned && NextSlot % 2 != 0;
  unsigned Start = NextSlot + Skips;
  NextSlot = Start + Words;

  // An empty struct is zero bytes in C and takes no slot; in C++ it is one
  // byte and takes one. A 16-byte-aligned one still starts at an even slot,
  // leaving an odd slot unused before the next argument.
  if (Bits == 0) {
    if (Skips)
      return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
    return ABIArgInfo::getIgnore();
  }

  if (isFloatVectorWord(Ty))
    return ABIArgInfo::getDirect(getSignificandsType(1));
  if (isSingleComplexFloat(Ty))
    return ABIArgInfo::getDirectInReg(getSignificandsType(1));

  llvm::ArrayType *Members = getHomogeneousType(Ty);
  if (!Members) {
    Members = getSingleMemberType(Ty);
    // GCC passes one of these that is partly on the stack as words rather
    // than members: each word in a register slot is the significand of an FP
    // register.
    if (Members && Start < 8 && NextSlot > 8)
      return ABIArgInfo::getDirect(
          getSignificandsType(Words), 0,
          Skips ? llvm::Type::getInt64Ty(getVMContext()) : nullptr);
  }
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

  // A floating-point vector is returned in f8 onward, one word per register
  // as the significand, since GCC treats it as a floating-point type that no
  // register type matches.
  if (const auto *VT = RetTy->getAs<VectorType>()) {
    QualType Element = VT->getElementType();
    if (Bits <= MaxResultBits &&
        (Element->isSpecificBuiltinType(BuiltinType::Float) ||
         Element->isSpecificBuiltinType(BuiltinType::Double)))
      return ABIArgInfo::getDirect(
          getSignificandsType(llvm::alignTo(Bits, 64) / 64));
  }

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
  // The address of an indirect result takes a slot only if it is passed as
  // an argument.
  unsigned NextSlot =
      FI.getReturnInfo().isIndirect() && FI.getReturnInfo().getInReg();
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type, NextSlot);
}

// Variadic arguments are laid out consecutively in memory, each in the slots
// it was passed in, so a 16-byte-aligned one is at a 16-byte-aligned
// address. This holds for a zero-sized one too, for which the caller left an
// odd slot unused.

RValue IA64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty, AggValueSlot Slot) const {
  bool IsIndirect = isInSlots(Ty) &&
                    getRecordArgABI(Ty, getCXXABI()) != CGCXXABI::RAA_Default;
  TypeInfoChars Info = getContext().getTypeInfoInChars(Ty);
  Info.Align = std::min(Info.Align, CharUnits::fromQuantity(16));
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, Info,
                          CharUnits::fromQuantity(8),
                          /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createIA64TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<IA64TargetCodeGenInfo>(CGM.getTypes());
}
