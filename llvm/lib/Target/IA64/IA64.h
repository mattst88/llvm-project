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
#include <memory>
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
class ScheduleDAGMutation;

FunctionPass *createIA64ISelDag(IA64TargetMachine &TM,
                                CodeGenOptLevel OptLevel);
FunctionPass *createIA64BreakAntiDepsPass();
FunctionPass *createIA64ExpandBranchesPass();
bool wantsPreservedBranchPredicates(const MachineFunction &MF);
FunctionPass *createIA64ExpandSpeculationPass();
FunctionPass *createIA64FoldPredicatedCopiesPass();
FunctionPass *createIA64ZextElimPass();
FunctionPass *createIA64SetfPhisPass();
FunctionPass *createIA64HoistFallThroughPass();
FunctionPass *createIA64HoistLoadUsesPass();
FunctionPass *createIA64HoistRestoresPass();
FunctionPass *createIA64IfConversionPass();
FunctionPass *createIA64InsertStopsPass();
FunctionPass *createIA64LocalDynamicTLSPass();
FunctionPass *createIA64PackGroupsPass();
FunctionPass *createIA64SpeculateLoadsPass();
FunctionPass *createIA64SplitSingleAddsPass();
FunctionPass *createIA64PostIncPointersPass();
FunctionPass *createIA64PrefetchStreamsPass();
FunctionPass *createIA64RematInCallLoopsPass();
FunctionPass *createIA64ReplicateDispatch();
FunctionPass *createIA64SinkPastCallsPass();
FunctionPass *createIA64SmallRangeMulPass();

/// A pipeliner mutation that issues the FP loads of different objects a
/// cycle apart, so that they do not meet in an L2 bank.
std::unique_ptr<ScheduleDAGMutation> createIA64LoadBanksMutation();

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
void initializeIA64ZextElimPass(PassRegistry &);
void initializeIA64SetfPhisPass(PassRegistry &);
void initializeIA64HoistFallThroughPass(PassRegistry &);
void initializeIA64HoistLoadUsesPass(PassRegistry &);
void initializeIA64HoistRestoresPass(PassRegistry &);
void initializeIA64IfConversionPass(PassRegistry &);
void initializeIA64InsertStopsPass(PassRegistry &);
void initializeIA64LocalDynamicTLSPass(PassRegistry &);
void initializeIA64PackGroupsPass(PassRegistry &);
void initializeIA64RematInCallLoopsPass(PassRegistry &);
void initializeIA64ReplicateDispatchPass(PassRegistry &);
void initializeIA64SinkPastCallsPass(PassRegistry &);
void initializeIA64SmallRangeMulPass(PassRegistry &);
void initializeIA64SpeculateLoadsPass(PassRegistry &);
void initializeIA64SplitSingleAddsPass(PassRegistry &);
void initializeIA64PostIncPointersPass(PassRegistry &);
void initializeIA64PrefetchStreamsPass(PassRegistry &);

struct IA64DeferMathErrnoPass : OptionalPassInfoMixin<IA64DeferMathErrnoPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64LoopAliasVersioningPass
    : OptionalPassInfoMixin<IA64LoopAliasVersioningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64WidenByteOpsPass : OptionalPassInfoMixin<IA64WidenByteOpsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64MergeByteStoresPass
    : OptionalPassInfoMixin<IA64MergeByteStoresPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64HoistCommonInstsPass
    : OptionalPassInfoMixin<IA64HoistCommonInstsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64ZextLoopStepsPass : OptionalPassInfoMixin<IA64ZextLoopStepsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

class IA64WidenIndicesPass
    : public OptionalPassInfoMixin<IA64WidenIndicesPass> {
  const IA64TargetMachine &TM;

public:
  explicit IA64WidenIndicesPass(const IA64TargetMachine &TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64LoadReusePass : OptionalPassInfoMixin<IA64LoadReusePass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64MulRecurrencePass : OptionalPassInfoMixin<IA64MulRecurrencePass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64SpeculateChasesPass
    : OptionalPassInfoMixin<IA64SpeculateChasesPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct IA64MemmoveVersioningPass
    : OptionalPassInfoMixin<IA64MemmoveVersioningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64_H
