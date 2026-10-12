//===-- IA64CallingConv.h - IA64 calling conventions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Argument and return value locations, shared by both instruction selectors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64CALLINGCONV_H
#define LLVM_LIB_TARGET_IA64_IA64CALLINGCONV_H

#include "llvm/CodeGen/CallingConvLower.h"

namespace llvm {

bool CC_IA64(unsigned ValNo, MVT ValVT, MVT LocVT, CCValAssign::LocInfo LocInfo,
             ISD::ArgFlagsTy ArgFlags, Type *OrigTy, CCState &State);
bool RetCC_IA64(unsigned ValNo, MVT ValVT, MVT LocVT,
                CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                Type *OrigTy, CCState &State);

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64CALLINGCONV_H
