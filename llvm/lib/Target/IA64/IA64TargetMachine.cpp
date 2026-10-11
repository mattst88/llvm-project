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
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64Target() {
  RegisterTargetMachine<IA64TargetMachine> X(getTheIA64Target());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeIA64ExpandBranchesPass(PR);
  initializeIA64InsertStopsPass(PR);
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
      : TargetPassConfig(TM, PM) {}

  IA64TargetMachine &getIA64TargetMachine() const {
    return getTM<IA64TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass2() override;
};
} // end anonymous namespace

void IA64PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool IA64PassConfig::addInstSelector() {
  addPass(createIA64ISelDag(getIA64TargetMachine(), getOptLevel()));
  return false;
}

void IA64PassConfig::addPreEmitPass2() {
  // Block placement is final, so branches can be split from their compares.
  addPass(createIA64ExpandBranchesPass());
  // Nothing may add, remove or reorder an instruction after this.
  addPass(createIA64InsertStopsPass());
}

TargetPassConfig *IA64TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new IA64PassConfig(*this, PM);
}
