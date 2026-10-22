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
#include <optional>

namespace llvm {

class FunctionPass;
class IA64RegisterBankInfo;
class LiveIntervals;
class MachineBasicBlock;
class ModuloSchedule;
class IA64Subtarget;
class IA64TargetMachine;
class InstructionSelector;
class PassRegistry;

FunctionPass *createIA64ISelDag(IA64TargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createIA64BreakAntiDepsPass();
FunctionPass *createIA64ExpandBranchesPass();
FunctionPass *createIA64ExpandSpeculationPass();
FunctionPass *createIA64FoldPredicatedCopiesPass();
FunctionPass *createIA64HoistFallThroughPass();
FunctionPass *createIA64HoistRestoresPass();
FunctionPass *createIA64IfConversionPass();
FunctionPass *createIA64InsertStopsPass();
FunctionPass *createIA64LocalDynamicTLSPass();
FunctionPass *createIA64PackGroupsPass();
FunctionPass *createIA64SpeculateLoadsPass();
FunctionPass *createIA64PostIncPointersPass();
FunctionPass *createIA64RematInCallLoopsPass();
InstructionSelector *
createIA64InstructionSelector(const IA64TargetMachine &TM,
                              const IA64Subtarget &STI,
                              const IA64RegisterBankInfo &RBI);

/// The most stages Loop may have when pipelined with rotating registers, or
/// 0 if it cannot or should not be. MaxTripCount bounds its iterations, if
/// known.
unsigned getIA64RotatingMaxStages(MachineBasicBlock *Loop,
                                  std::optional<uint64_t> MaxTripCount);
/// Expand MS into a kernel-only loop that rotates registers, if it can be.
bool expandIA64RotatingSchedule(ModuloSchedule &MS, LiveIntervals &LIS);

void initializeIA64BreakAntiDepsPass(PassRegistry &);
void initializeIA64ExpandBranchesPass(PassRegistry &);
void initializeIA64ExpandSpeculationPass(PassRegistry &);
void initializeIA64FoldPredicatedCopiesPass(PassRegistry &);
void initializeIA64HoistFallThroughPass(PassRegistry &);
void initializeIA64HoistRestoresPass(PassRegistry &);
void initializeIA64IfConversionPass(PassRegistry &);
void initializeIA64InsertStopsPass(PassRegistry &);
void initializeIA64LocalDynamicTLSPass(PassRegistry &);
void initializeIA64PackGroupsPass(PassRegistry &);
void initializeIA64RematInCallLoopsPass(PassRegistry &);
void initializeIA64SpeculateLoadsPass(PassRegistry &);
void initializeIA64PostIncPointersPass(PassRegistry &);

struct IA64LoopAliasVersioningPass
    : OptionalPassInfoMixin<IA64LoopAliasVersioningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64MemmoveVersioningPass
    : OptionalPassInfoMixin<IA64MemmoveVersioningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64_H
