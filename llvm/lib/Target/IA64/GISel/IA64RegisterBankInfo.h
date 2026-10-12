//===-- IA64RegisterBankInfo.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the targeting of the RegisterBankInfo class for IA64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_GISEL_IA64REGISTERBANKINFO_H
#define LLVM_LIB_TARGET_IA64_GISEL_IA64REGISTERBANKINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include <memory>

#define GET_REGBANK_DECLARATIONS
#include "IA64GenRegisterBank.inc"
#undef GET_REGBANK_DECLARATIONS

namespace llvm {

class TargetRegisterInfo;

class IA64GenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "IA64GenRegisterBank.inc"
#undef GET_TARGET_REGBANK_CLASS
};

class IA64RegisterBankInfo final : public IA64GenRegisterBankInfo {
public:
  IA64RegisterBankInfo();

  const InstructionMapping &
  getInstrMapping(const MachineInstr &MI) const override;

private:
  /// The 64-bit word mappings for each size wider than a register. A
  /// ValueMapping keeps pointers to its parts, so they are owned here.
  mutable DenseMap<unsigned, std::unique_ptr<PartialMapping[]>> WordsOf;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_GISEL_IA64REGISTERBANKINFO_H
