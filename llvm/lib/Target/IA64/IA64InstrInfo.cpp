//===-- IA64InstrInfo.cpp - IA64 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "IA64GenInstrInfo.inc"

using namespace llvm;

IA64InstrInfo::IA64InstrInfo(const IA64Subtarget &STI)
    : IA64GenInstrInfo(STI, RI, IA64::ADJCALLSTACKDOWN, IA64::ADJCALLSTACKUP),
      RI() {}
