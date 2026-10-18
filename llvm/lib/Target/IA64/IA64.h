//===-- IA64.h - Top-level interface for IA64 representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the
// LLVM IA64 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64_H
#define LLVM_LIB_TARGET_IA64_IA64_H

#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class IA64RegisterBankInfo;
class IA64Subtarget;
class IA64TargetMachine;
class InstructionSelector;
class PassRegistry;

FunctionPass *createIA64ISelDag(IA64TargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createIA64ExpandBranchesPass();
FunctionPass *createIA64IfConversionPass();
FunctionPass *createIA64InsertStopsPass();
FunctionPass *createIA64LocalDynamicTLSPass();
FunctionPass *createIA64PackGroupsPass();
InstructionSelector *
createIA64InstructionSelector(const IA64TargetMachine &TM,
                              const IA64Subtarget &STI,
                              const IA64RegisterBankInfo &RBI);

void initializeIA64ExpandBranchesPass(PassRegistry &);
void initializeIA64IfConversionPass(PassRegistry &);
void initializeIA64InsertStopsPass(PassRegistry &);
void initializeIA64LocalDynamicTLSPass(PassRegistry &);
void initializeIA64PackGroupsPass(PassRegistry &);

struct IA64LoopAliasVersioningPass
    : OptionalPassInfoMixin<IA64LoopAliasVersioningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64_H
