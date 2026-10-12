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

  bool supportsTailCallFor(const CallBase *CB) const override {
    return TLI->supportsTailCallFor(CB);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64TARGETTRANSFORMINFO_H
