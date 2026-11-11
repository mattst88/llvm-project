//===-- IA64TargetTransformInfo.h - IA64 specific TTI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// IA64 target transform information for IR-level passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_IA64_IA64TARGETTRANSFORMINFO_H

#include "IA64Subtarget.h"
#include "IA64TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class IA64TTIImpl final : public BasicTTIImplBase<IA64TTIImpl> {
  using BaseT = BasicTTIImplBase<IA64TTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const IA64Subtarget *ST;
  const IA64TargetLowering *TLI;

  const IA64Subtarget *getST() const { return ST; }
  const IA64TargetLowering *getTLI() const { return TLI; }

public:
  explicit IA64TTIImpl(const IA64TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  // Post-increment of the address register is the only addressing mode
  // loads and stores have, so loops should step a pointer.
  TTI::AddressingModeKind
  getPreferredAddressingMode(const Loop *L,
                             ScalarEvolution *SE) const override {
    return TTI::AMK_PostIndexed;
  }

  // popcnt counts the bits of a 64-bit register, and of narrower values once
  // the upper bits are cleared.
  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth) const override {
    assert(isPowerOf2_32(TyWidth) && "a width that is not a power of two");
    return TyWidth <= 64 ? TTI::PSK_FastHardware : TTI::PSK_Software;
  }

  // The cost of materializing a constant, and the cost of a constant operand
  // to an instruction, which is free if the instruction has an immediate
  // form. A constant costing more than an instruction is worth hoisting into
  // a register for all its uses.
  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) const override;
  InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr) const override;
  InstructionCost
  getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                      Type *Ty, TTI::TargetCostKind CostKind) const override;

  // Integer multiplication is done in the FP registers, integer division is
  // a libcall, and FP division is a refinement sequence starting from a
  // reciprocal approximation.
  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {},
      const Instruction *CtxI = nullptr) const override;

  // An in-order machine that issues six instructions a cycle has nothing to
  // overlap one iteration of a small loop with but the next.
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override;

  // br.cloop counts a loop down in ar.lc, which is callee-saved, so a
  // counted loop needs no induction variable of its own to compare.
  bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                AssumptionCache &AC, TargetLibraryInfo *LibInfo,
                                HardwareLoopInfo &HWLoopInfo) const override;

  // Count instructions in a loop before registers, as PowerPC does. This
  // often gives each stream a pointer of its own, which the pipeliner
  // relates by the constant distance between their start addresses.
  bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                     const TargetTransformInfo::LSRCost &C2) const override {
    return std::tie(C1.Insns, C1.NumRegs, C1.AddRecCost, C1.NumIVMuls,
                    C1.NumBaseAdds, C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.Insns, C2.NumRegs, C2.AddRecCost, C2.NumIVMuls,
                    C2.NumBaseAdds, C2.ScaleCost, C2.ImmCost, C2.SetupCost);
  }

  // An inner loop split out of one with several backedges is set up again
  // each time the outer one goes round, for which it pays a branch and its
  // preheader.
  bool shouldSeparateNestedLoops() const override { return false; }

  // A call costs a register stack frame, saved branch and gp registers and a
  // stop each side, and keeps the code around it from being scheduled with
  // the callee's. Inlining twice as much made tramp3d 5.3%, coremark 8.3%
  // and linpack 8.5% faster on hardware, and nothing slower.
  unsigned getInliningThresholdMultiplier() const override { return 2; }

  // A function that every unit using it emits, as a C++ template or inline
  // member is, is dropped once its last call is inlined, and is most often a
  // small accessor or functor that an abstraction layer calls in a loop.
  // Half the usual threshold again for those made tramp3d 4.8% faster on
  // hardware.
  unsigned adjustInliningThreshold(const CallBase *CB) const override {
    const Function *Callee = CB->getCalledFunction();
    if (!Callee || !Callee->hasLinkOnceODRLinkage() ||
        CB->getCaller()->hasOptSize())
      return 0;
    return 113;
  }

  // Loads that step a line of L1D or more each iteration are prefetched about
  // 100 instructions ahead, enough to hide a hit in L3; farther ahead was
  // slower in bzip2. Prefetching a line in every iteration of a loop that
  // steps less than one cost more than it saved.
  // On a Madison, interleave loops that store a floating-point value by two,
  // behind run-time checks where the arrays may overlap.  linpack takes 7%
  // fewer cycles there.  Loops that store no floating-point value did not
  // help, and four copies cost gemm 12% and fdtd2d 15% in the model.  On an
  // Itanium 9340 the same two copies made cholesky 7.6%, gemm 4.8% and
  // linpack 3.5% slower.
  unsigned getMaxInterleaveFactor(ElementCount VF,
                                  bool HasUnorderedReductions) const override {
    return VF.isScalar() && ST->isMadison()
               ? 2
               : BaseT::getMaxInterleaveFactor(VF, HasUnorderedReductions);
  }
  bool enableScalarInterleaving(const Loop *L) const override;
  bool enableScalarInterleavingWithRuntimeChecks() const override {
    return true;
  }
  // Floating-point values have a register file of their own.  The integer
  // class stays at the default: a real count there cost sqlite 0.8% and
  // tramp3d 2.4% once loops interleaved.
  enum { FPRC = 2 };
  unsigned getRegisterClassForType(bool Vector,
                                   Type *Ty = nullptr) const override {
    if (!Vector && Ty && Ty->getScalarType()->isFloatingPointTy())
      return FPRC;
    return BaseT::getRegisterClassForType(Vector, Ty);
  }
  const char *getRegisterClassName(unsigned ClassID) const override {
    return ClassID == FPRC ? "IA64::FPRC"
                           : BaseT::getRegisterClassName(ClassID);
  }
  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    return ClassID == FPRC ? 96 : BaseT::getNumberOfRegisters(ClassID);
  }

  unsigned getCacheLineSize() const override { return 64; }
  unsigned getPrefetchDistance() const override { return 100; }
  unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                unsigned NumStridedMemAccesses,
                                unsigned NumPrefetches,
                                bool HasCall) const override {
    return 64;
  }

  // Floating-point loads read L2, not L1D, and an lfetch for one only fills
  // L1D or takes L2 bandwidth from them: without its three, a Polybench
  // cholesky loop of column loads ran 10% faster on an Itanium 9340.
  bool shouldPrefetchMemAccess(const Instruction &I) const override {
    return !I.getType()->getScalarType()->isFloatingPointTy();
  }

  bool supportsTailCallFor(const CallBase *CB) const override {
    return TLI->supportsTailCallFor(CB);
  }

  // A relative lookup table lives in .rodata, in the text segment, and its
  // 32-bit entries may point into the data segment, which is a region of the
  // address space 2^61 bytes away.
  bool shouldBuildRelLookupTables() const override { return false; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64TARGETTRANSFORMINFO_H
