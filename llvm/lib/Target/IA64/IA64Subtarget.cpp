//===-- IA64Subtarget.cpp - IA64 Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64Subtarget.h"
#include "IA64.h"
#include "IA64TargetMachine.h"

#define DEBUG_TYPE "ia64-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "IA64GenSubtargetInfo.inc"

using namespace llvm;

static cl::opt<bool>
    EnablePipeliner("ia64-pipeliner", cl::Hidden, cl::init(true),
                    cl::desc("Software pipeline loops counted in ar.lc"));

bool IA64Subtarget::enableMachinePipeliner() const {
  return EnablePipeliner && getSchedModel().hasInstrSchedModel();
}

IA64Subtarget &IA64Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                              StringRef TuneCPU,
                                                              StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  if (TuneCPU.empty())
    TuneCPU = CPU;
  ParseSubtargetFeatures(CPU, TuneCPU, FS);
  return *this;
}

IA64Subtarget::IA64Subtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                             StringRef FS, const TargetMachine &TM)
    : IA64GenSubtargetInfo(TT, CPU, TuneCPU.empty() ? CPU : TuneCPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      TLInfo(static_cast<const IA64TargetMachine &>(TM), *this),
      FrameLowering(*this), CallLoweringInfo(TLInfo),
      InlineAsmLoweringInfo(&TLInfo), Legalizer(*this) {
  InstSelector.reset(createIA64InstructionSelector(
      static_cast<const IA64TargetMachine &>(TM), *this, RegBankInfo));
}
