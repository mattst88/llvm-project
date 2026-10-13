//===--- IA64.h - Declare IA-64 target feature support ------------*- C++
//-*===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares IA-64 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H

#include "clang/Basic/TargetInfo.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY IA64TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];

public:
  IA64TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // Little-endian LP64, as on Linux.
    LongWidth = LongAlign = 64;
    PointerWidth = PointerAlign = 64;
    IntMaxType = SignedLong;
    Int64Type = SignedLong;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    WCharType = SignedInt;
    WIntType = UnsignedInt;

    // long double is the FP registers' double-extended format, stored in 16
    // bytes; quad precision is a separate type.
    LongDoubleWidth = LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
    HasFloat128 = true;

    // Every FP instruction rounds according to the FP status register and
    // records its exceptions there.
    HasStrictFP = true;

    SuitableAlign = 128;

    // Virtual tables hold function descriptors.
    VTableUsesFunctionDescriptors = true;
    // 16-byte atomic objects are 16-byte aligned, as in GCC, though operating
    // on one always requires libatomic.
    MaxAtomicPromoteWidth = 128;
    MaxAtomicInlineWidth = 64;

    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "ia64";
  }

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string_view getClobbers() const override { return ""; }

  // Landing pads receive the exception data in r15-r18, the scratch registers
  // GCC's unwinder uses for it.
  int getEHDataRegisterNumber(unsigned RegNo) const override {
    return RegNo < 4 ? 15 + RegNo : -1;
  }

  // A bit-precise integer is passed as the integer of the next size up is,
  // and the widest integer there is has 128 bits.
  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override { return 128; }

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H
