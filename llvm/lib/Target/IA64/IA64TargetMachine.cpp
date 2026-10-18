//===-- IA64TargetMachine.cpp - Define TargetMachine for IA64 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64TargetMachine.h"
#include "IA64.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64TargetObjectFile.h"
#include "IA64TargetTransformInfo.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

static cl::opt<bool>
    EnableCountedLoops("ia64-counted-loops", cl::Hidden, cl::init(true),
                       cl::desc("Count loops down in ar.lc with br.cloop"));

static cl::opt<bool> EnableAliasVersioning(
    "ia64-alias-versioning", cl::Hidden, cl::init(true),
    cl::desc("Version small loops on whether their pointers overlap"));

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64Target() {
  RegisterTargetMachine<IA64TargetMachine> X(getTheIA64Target());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeGlobalISel(PR);
  initializeIA64ExpandBranchesPass(PR);
  initializeIA64IfConversionPass(PR);
  initializeIA64InsertStopsPass(PR);
  initializeIA64LocalDynamicTLSPass(PR);
  initializeIA64PackGroupsPass(PR);
}

// Code addresses data relative to GP and other code relative to IP, so it is
// position independent by default. The relocation model only decides whether
// a symbol may be preemptible.
static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::PIC_);
}

IA64TargetMachine::IA64TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<IA64TargetObjectFile>()) {
  initAsmInfo();

  // GlobalISel is opt-in, and falls back to SelectionDAG with a diagnostic
  // for each function it cannot select.
  setGlobalISelAbort(GlobalISelAbortMode::DisableWithDiag);
}

MachineFunctionInfo *IA64TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return IA64MachineFunctionInfo::create<IA64MachineFunctionInfo>(Allocator, F,
                                                                  STI);
}

const IA64Subtarget *
IA64TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + TuneCPU + FS];
  if (!I)
    I = std::make_unique<IA64Subtarget>(TargetTriple, CPU, TuneCPU, FS, *this);
  return I.get();
}

namespace {
class IA64PassConfig : public TargetPassConfig {
public:
  IA64PassConfig(IA64TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    // Also schedule after register allocation, with the same scheduler.
    if (TM.getOptLevel() != CodeGenOptLevel::None)
      substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
  }

  IA64TargetMachine &getIA64TargetMachine() const {
    return getTM<IA64TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  bool addLegalizeMachineIR() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addMachineSSAOptimization() override;
  bool addILPOpts() override;
  void addPreRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
};
} // end anonymous namespace

void IA64PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  if (getOptLevel() != CodeGenOptLevel::None && EnableCountedLoops)
    addPass(createHardwareLoopsLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool IA64PassConfig::addInstSelector() {
  addPass(createIA64ISelDag(getIA64TargetMachine(), getOptLevel()));
  return false;
}

bool IA64PassConfig::addIRTranslator() {
  addPass(new IRTranslatorLegacy(getOptLevel()));
  return false;
}

bool IA64PassConfig::addLegalizeMachineIR() {
  addPass(new LegalizerLegacy());
  return false;
}

bool IA64PassConfig::addRegBankSelect() {
  addPass(new RegBankSelectLegacy());
  return false;
}

bool IA64PassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelectLegacy(getOptLevel()));
  return false;
}

// Removing redundant __tls_get_addr calls leaves dead instructions, which
// later passes remove.
void IA64PassConfig::addMachineSSAOptimization() {
  addPass(createIA64LocalDynamicTLSPass());
  TargetPassConfig::addMachineSSAOptimization();
}

bool IA64PassConfig::addILPOpts() {
  addPass(&MachineCombinerID);
  return true;
}

void IA64PassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&MachinePipelinerID);
}

void IA64PassConfig::addPreSched2() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createIA64IfConversionPass());
}

void IA64PassConfig::addPreEmitPass() { addPass(&BranchRelaxationPassID); }

void IA64PassConfig::addPreEmitPass2() {
  // Block placement is final, so branches can be split from their compares.
  addPass(createIA64ExpandBranchesPass());
  // Nothing may add, remove or reorder an instruction after this, except
  // within the instruction groups it forms.
  addPass(createIA64InsertStopsPass());
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createIA64PackGroupsPass());
}

TargetTransformInfo
IA64TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<IA64TTIImpl>(this, F));
}

void IA64TargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
#define GET_PASS_REGISTRY "IA64PassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"

  // Before the vectorizer and the unroller, which copy the versioned loop.
  PB.registerVectorizerStartEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (EnableAliasVersioning &&
            (Level == OptimizationLevel::O2 || Level == OptimizationLevel::O3))
          FPM.addPass(IA64LoopAliasVersioningPass());
      });
}

TargetPassConfig *IA64TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new IA64PassConfig(*this, PM);
}
