//===- IA64.cpp
//------------------------------------------------------------===//
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
// IA-64 ABI Implementation. The software conventions give each argument one
// or more of the eight slots that r32 to r39 are, and a floating-point one a
// register of f8 to f15 as well; a scalar is what the default ABI makes of it.
//===----------------------------------------------------------------------===//

namespace {
class IA64ABIInfo : public DefaultABIInfo {
public:
  IA64ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}
};

class IA64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  IA64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<IA64ABIInfo>(CGT)) {}
};
} // end anonymous namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createIA64TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<IA64TargetCodeGenInfo>(CGM.getTypes());
}
